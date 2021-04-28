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
// \file power_control.h
// \brief This file defines the functions for power related changes to drives.

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

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
    OPENSEA_OPERATIONS_API int enable_Disable_EPC_Feature(tDevice *device, eEPCFeatureSet lba_field);

    //-----------------------------------------------------------------------------
    //
    //  print_Current_Power_Mode( tDevice * device )
    //
    //! \brief   Checks the current power mode of the device and prints it to the screen. (SATA will always work. SAS only works if the drive has transitioned to another state)
    //
    //  Entry:
    //!   \param  device file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int print_Current_Power_Mode(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  set_Device_Power_Mode( tDevice * device )
    //
    //! \brief   Set the power modes, their timers, or restore default settings. This can only do EPC settings, and single power states at a time.
    //!          This function has been replaced by a set_EPC_Power_Conditions below which can handle changing multiple power states at a time.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param restoreDefaults - set to true to restore the drives default settings. All other options will be ignored
    //!   \param enableDisable - set to true for enabling a powerCondition and false to disable one
    //!   \param powerCondition - set to the power condition you wish to have changed
    //!   \param powerModeTimer - the timer value to set. If this is zero, a timer value will not be set and the power mode will only be enabled. This option is ignored when enableDisable is false or when restoreDefaults is true
    //!   \param powerModeTimerValid - when set to true, this means the incoming timer value is valid
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid);

    OPENSEA_OPERATIONS_API int scsi_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid);

    OPENSEA_OPERATIONS_API int ata_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid);

    //-----------------------------------------------------------------------------
    //
    //  transition_Power_State( tDevice * device, ePowerConditionID newState); )
    //
    //! \brief  Transition the device from one power state to another. 
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param newState - State the device is desired to be transitioned to. 
    //!                     e.g. PWR_CND_IDLE_A, PWR_CND_IDLE_B etc. 
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int transition_Power_State(tDevice *device, ePowerConditionID newState);

#if !defined(DISABLE_NVME_PASSTHROUGH)
    OPENSEA_OPERATIONS_API int transition_NVM_Power_State(tDevice *device, uint8_t newState);
#endif

    //-----------------------------------------------------------------------------
    //
    //  transition_Power_State( tDevice * device, ePowerConditionID newState); )
    //
    //! \brief  Transition the device from one power state to another. 
    //
    //  Entry:
    //!   \param [in] device - file descriptor
    //!   \param [out] powerState - The power state the device currently is in
    //!   \param [in] selectValue - enum to say Current or default etc. 
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Power_State(tDevice *device, uint32_t * powerState, eFeatureModeSelect selectValue );

    typedef struct _powerConsumptionIdentifier
    {
        uint8_t identifierValue;//see SPC spec
        uint8_t units;//matches SPC spec
        uint16_t value;//matches SPC spec
    }powerConsumptionIdentifier;

    typedef struct _powerConsumptionIdentifiers
    {
        uint8_t numberOfPCIdentifiers;
        powerConsumptionIdentifier identifiers[0xFF];//Maximum number of power consumption identifiers...probably won't get this many, but might as well make this possible to do.
        bool currentIdentifierValid;
        uint8_t currentIdentifier;
        bool activeLevelChangable;//this may be changable or not depending on what the drive reports
        uint8_t activeLevel;//From power consumption mode page. This is a high/medium/low value. Only use this if non-zero
    }powerConsumptionIdentifiers, *ptrPowerConsumptionIdentifiers;

    //-----------------------------------------------------------------------------
    //
    //  get_Power_Consumption_Identifiers(tDevice *device, ptrPowerConsumptionIdentifiers identifiers)
    //
    //! \brief  Get all the power consumption identifiers from a SCSI device.
    //
    //  Entry:
    //!   \param [in] device - file descriptor
    //!   \param [out] identifiers - pointer to a struct that will be filled in with power consumption information
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Power_Consumption_Identifiers(tDevice *device, ptrPowerConsumptionIdentifiers identifiers);

    //-----------------------------------------------------------------------------
    //
    //  print_Power_Consumption_Identifiers(ptrPowerConsumptionIdentifiers identifiers)
    //
    //! \brief  Print out all the power consumption identifiers in the struct (the number that is supported anyways).
    //
    //  Entry:
    //!   \param [in] identifiers - pointer to a struct containing the power consumption identifier information
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Power_Consumption_Identifiers(ptrPowerConsumptionIdentifiers identifiers);

    typedef enum _ePCActiveLevel
    {
        PC_ACTIVE_LEVEL_IDENTIFIER = 0,
        PC_ACTIVE_LEVEL_HIGHEST = 1,
        PC_ACTIVE_LEVEL_INTERMEDIATE = 2,
        PC_ACTIVE_LEVEL_LOWEST = 3
    }ePCActiveLevel;

    //-----------------------------------------------------------------------------
    //
    //  set_Power_Consumption(tDevice *device, ePCActiveLevel activeLevelField, uint8_t powerConsumptionIdentifier, bool resetToDefault)
    //
    //! \brief  Set the power consumption rate for a SCSI drive. (ATA not supported right now, may never be depending on future ata spec implementation)
    //
    //  Entry:
    //!   \param [in] device - file descriptor
    //!   \param [in] activeLevelField - set to an enum value matching SCP spec. When set to PC_ACTIVE_LEVEL_IDENTIFIER, the powerConsumptionIdentifier value will be used.
    //!   \param [in] powerConsumptionIdentifier - only valid when activeLevelField is set to PC_ACTIVE_LEVEL_IDENTIFIER. This value must match one the device supports.
    //!   \param [in] resetToDefault - when set to true, all other inputs are ignored. The default mode is restored by reading the default settings and setting the current settings to the defaults.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_Power_Consumption(tDevice *device, ePCActiveLevel activeLevelField, uint8_t powerConsumptionIdentifier, bool resetToDefault);

    //-----------------------------------------------------------------------------
    //
    //  map_Watt_Value_To_Power_Consumption_Identifier(tDevice *device, double watts, uint8_t *powerConsumptionIdentifier)
    //
    //! \brief  Maps a value in Watts to a power condition identifier supported by the drive. Will return NOT_SUPPORTED if no suitable match can be found
    //
    //  Entry:
    //!   \param [in] device - file descriptor
    //!   \param [in] watts - value in watts to search for. Will be rounded down when searching for a match.
    //!   \param [out] powerConsumptionIdentifier - will be set to the value of the power consumption identifier that most closely matches the incoming watt value.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int map_Watt_Value_To_Power_Consumption_Identifier(tDevice *device, double watts, uint8_t *powerConsumptionIdentifier);
    
    OPENSEA_OPERATIONS_API int enable_Disable_APM_Feature(tDevice *device, bool enable);
    
    OPENSEA_OPERATIONS_API int set_APM_Level(tDevice *device, uint8_t apmLevel);
    
    OPENSEA_OPERATIONS_API int get_APM_Level(tDevice *device, uint8_t *apmLevel);

    typedef struct _powerConditionInfo //written according to ATA spec fields...will try to populate as much as possible that's similar for SCSI
    {
        bool powerConditionSupported;
        bool powerConditionSaveable;
        bool powerConditionChangeable;
        bool defaultTimerEnabled;
        bool savedTimerEnabled;
        bool currentTimerEnabled;
        bool holdPowerConditionNotSupported;
        uint32_t defaultTimerSetting;
        uint32_t savedTimerSetting;
        uint32_t currentTimerSetting;
        uint32_t nominalRecoveryTimeToActiveState;
        uint32_t minimumTimerSetting;
        uint32_t maximumTimerSetting;
    }powerConditionInfo, *ptrPowerConditionInfo;

    typedef struct _epcSettings
    {
        powerConditionInfo idle_a;
        powerConditionInfo idle_b;
        powerConditionInfo idle_c;
        powerConditionInfo standby_y;
        powerConditionInfo standby_z;
        bool               settingsAffectMultipleLogicalUnits;
    }epcSettings, *ptrEpcSettings;

    OPENSEA_OPERATIONS_API int get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings);

    OPENSEA_OPERATIONS_API void print_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings);

    OPENSEA_OPERATIONS_API int sata_Get_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool *supported, bool *enabled);

    OPENSEA_OPERATIONS_API int sata_Set_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool enable);

    OPENSEA_OPERATIONS_API int sata_Get_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool *supported, bool *enabled);

    OPENSEA_OPERATIONS_API int sata_Set_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool enable);


    //Following functions allow power mode transitions on Non-EPC drives (pre SBC3 for SCSI)
    //These will work with EPC, but may cause changes to the current timer values. See SCSI/ATA specs for details on interactions with these and EPC timers
    OPENSEA_OPERATIONS_API int transition_To_Active(tDevice *device);

    OPENSEA_OPERATIONS_API int transition_To_Standby(tDevice *device);

    OPENSEA_OPERATIONS_API int transition_To_Idle(tDevice *device, bool unload); //unload feature must be supported

    //NOTE: Do not call this unless you know what you are doing. This requires a reset to wake up from, which may not be callable from an application.
    OPENSEA_OPERATIONS_API int transition_To_Sleep (tDevice *device);

    //Be careful changing partial and slumber settings. Not every controller will support it properly!
    OPENSEA_OPERATIONS_API int scsi_Set_Partial_Slumber(tDevice *device, bool enablePartial, bool enableSlumber, bool partialValid, bool slumberValid, bool allPhys, uint8_t phyNumber);

    OPENSEA_OPERATIONS_API int get_SAS_Enhanced_Phy_Control_Number_Of_Phys(tDevice *device, uint8_t *phyCount);

    typedef struct _sasEnhPhyControl
    {
        uint8_t phyIdentifier;
        bool enablePartial;
        bool enableSlumber;
    }sasEnhPhyControl, *ptrSasEnhPhyControl;
    //If doing all phys, use get_SAS_Enhanced_Phy_Control_Number_Of_Phys first to figure out how much memory must be allocated
    OPENSEA_OPERATIONS_API int get_SAS_Enhanced_Phy_Control_Partial_Slumber_Settings(tDevice *device, bool allPhys, uint8_t phyNumber, ptrSasEnhPhyControl enhPhyControlData, uint32_t enhPhyControlDataSize);

    OPENSEA_OPERATIONS_API void show_SAS_Enh_Phy_Control_Partial_Slumber(ptrSasEnhPhyControl enhPhyControlData, uint32_t enhPhyControlDataSize, bool showPartial, bool showSlumber);

    typedef struct _powerConditionSettings
    {
        bool powerConditionValid;//specifies whether to look at anything in this structure or not.
        bool restoreToDefault;//If this is set, none of the other values in this matter
        bool enableValid;//holds if enable bool below should be referenced at all or not.
        bool enable;//set to false to disable when enableValid is set to true
        bool timerValid;//set to true when the below timer has a valid value to use
        uint32_t timerInHundredMillisecondIncrements;
    }powerConditionSettings, *ptrPowerConditionSettings;

    typedef struct _powerConditionTimers
    {
        union {
            powerConditionSettings idle;
            powerConditionSettings idle_a;
        };
        union {
            powerConditionSettings standby;
            powerConditionSettings standby_z;
        };
        //All fields below here only apply to EPC. Everything above is supported by legacy devices that support the power conditions mode page
        powerConditionSettings idle_b;
        powerConditionSettings idle_c;
        powerConditionSettings standby_y;
        //Fields below are only for SAS/SCSI devices. Attempting to change these for other devices will be ignored.
        //PM_BG_Preference and CCF fields are handled below. EPC only
        bool powerModeBackgroundValid;
        bool powerModeBackgroundResetDefault;//reset this to default value
        uint8_t powerModeBackGroundRelationShip;
        struct
        {
            bool ccfIdleValid;
            bool ccfStandbyValid;
            bool ccfStopValid;
            bool ccfIdleResetDefault;
            bool ccfStandbyResetDefault;
            bool ccfStopResetDefault;
            uint8_t ccfIdleMode;
            uint8_t ccfStandbyMode;
            uint8_t ccfStopMode;
        }checkConditionFlags;
    }powerConditionTimers, *ptrPowerConditionTimers;

    OPENSEA_OPERATIONS_API int scsi_Set_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions);

    OPENSEA_OPERATIONS_API int set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions);

    OPENSEA_OPERATIONS_API int scsi_Set_Legacy_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionSettings standbyTimer, ptrPowerConditionSettings idleTimer);

    OPENSEA_OPERATIONS_API int scsi_Set_Standby_Timer_State(tDevice *device, bool enable);

    //When ATA drive, the restoreToDefaults is not allowed. Also, translation of timer value is done according to SAT spec
    OPENSEA_OPERATIONS_API int set_Standby_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault);

    //SCSI/SAS Only
    OPENSEA_OPERATIONS_API int scsi_Set_Idle_Timer_State(tDevice *device, bool enable);
    OPENSEA_OPERATIONS_API int set_Idle_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault);

#if defined (__cplusplus)
}
#endif
