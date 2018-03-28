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
// \file seagate_operations.h
// \brief This file defines the functions for Seagate drive specific operations

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    typedef enum _eSeagateSCTBISTFuncCodes{
        BIST_SET_SATA_PHY_SPEED = 0x0003,
        BIST_CHECK_SATA_PHY_SPEED = 0x0004
    }eSeagateSCTBISTFuncCodes;

#define SCT_SEAGATE_SPEED_CONTROL SCT_RESERVED_FOR_SATA

    //-----------------------------------------------------------------------------
    //
    //  seagate_ata_SCT_SATA_phy_speed()
    //
    //! \brief   Description:  This issues a Seagate Specific SCT command to change the SATA PHY speed. Only available on Seagate HDD's
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param useGPL - set to true to use read/write log ext instead of SMART commands
    //!   \param useDMA - only valid when useGPL is set. Set to true to issue DMA commands instead of PIO commands
    //!   \param speedGen - Which SATA generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s. All other inputs return BAD_PARAMETER
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int seagate_ata_SCT_SATA_phy_speed(tDevice *device, bool useGPL, bool useDMA, uint8_t speedGen);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber)
    //
    //! \brief   Description:  This issues a mode sense and mode select to the SAS phy page to change the programmed maximum link rate of 1 or all phys.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param phySpeedGen - Which SAS generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s, 4 = 12.0Gb/s, 5 = 22.5Gb/s All other inputs return BAD_PARAMETER
    //!   \param allPhys - on SAS, set this to true to set all Phys to the same speed.
    //!   \param phyNumber - if allPhys is false, this is used to specify which phy to change speed on.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber);

    //-----------------------------------------------------------------------------
    //
    //  set_phy_speed()
    //
    //! \brief   Description:  This is the friendly call to use that does all the input checking for you before calling the proper functions to set Phy Speed for SATA or SAS.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param phySpeedGen - Which generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s, 4 = 12.0Gb/s (SAS), 5 = 22.5Gb/s (SAS). All other inputs return BAD_PARAMETER
    //!   \param allPhys - on SAS, set this to true to set all Phys to the same speed. This is ignored on SATA
    //!   \param phyIdentifier - if allPhys is false, this is used to specify which phy to change speed on. This is ignored on SATA
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_phy_speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyIdentifier);

    //-----------------------------------------------------------------------------
    //
    //  is_Low_Current_Spin_Up_Enabled(tDevice *device)
    //
    //! \brief   Description:  This function will check if low current spin up is enabled on Seagate ATA drives. Not all drives support this feature.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return true = low current spinup enabled, false = not enabled or not supported.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Low_Current_Spin_Up_Enabled(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  enable_Low_Current_Spin_Up(tDevice *device)
    //
    //! \brief   Description:  This function will send the command to enable low current spinup on Seagate ATA drives. (Not all drives support this feature) Power cycle required to complete this change.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully enabled low current spin up, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int enable_Low_Current_Spin_Up(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  disable_Low_Current_Spin_Up(tDevice *device)
    //
    //! \brief   Description:  This function will send the command to disable low current spinup on Seagate ATA drives. (Not all drives support this feature) Power cycle required to complete this change.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully disabled low current spin up, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int disable_Low_Current_Spin_Up(tDevice *device);

    typedef enum _eSSCFeatureState
    {
        SSC_DEFAULT = 0,
        SSC_ENABLED = 1,
        SSC_DISABLED = 2
    }eSSCFeatureState;

    //-----------------------------------------------------------------------------
    //
    //  set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode)
    //
    //! \brief   Description:  This function will send the command to set the SSC (Spread Spectrum Clocking) state of a Seagate SATA drive. A power cycle is required to make changes take affect
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param mode - set to enum value saying whether to enable, disable, or set to defaults
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set SSC state, FAILURE = failed to set SSC state, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode);

    //-----------------------------------------------------------------------------
    //
    //  get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode)
    //
    //! \brief   Description:  This function will send the command to get the SSC (Spread Spectrum Clocking) state of a Seagate SATA drive.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param mode - pointer to enum variable that will hold the state upon successful completion
    //!
    //  Exit:
    //!   \return SUCCESS = successfully got SSC state, FAILURE = failed to get SSC state, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode);

    typedef struct _seagateJITModes
    {
        bool valid;//this must be set to true for the remaining fileds to have meaning.
        bool vJIT;//variable...drive will use the fastest method
        bool jit0;//fastest
        bool jit1;//second fastest
        bool jit2;//second slowest
        bool jit3;//slowest
    }seagateJITModes, *ptrSeagateJITModes;

    OPENSEA_OPERATIONS_API int seagate_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile);

    OPENSEA_OPERATIONS_API int seagate_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes);

    OPENSEA_OPERATIONS_API int seagate_Get_Power_Balance(tDevice *device, bool *supported, bool *enabled);//SATA only. SAS should use the set power consumption options in power_control.h

    OPENSEA_OPERATIONS_API int seagate_Set_Power_Balance(tDevice *device, bool enable);//SATA only. SAS should use the set power consumption options in power_control.h

	typedef enum _eIDDTests
	{
		SEAGATE_IDD_SHORT,
		SEAGATE_IDD_LONG,
	}eIDDTests;

	typedef struct _iddSupportedFeatures
	{
		bool iddShort;//reset and recalibrate
		bool iddLong;//testPendingAndReallocationLists
	}iddSupportedFeatures, *ptrIDDSupportedFeatures;

	//-----------------------------------------------------------------------------
	//
	//  get_IDD_Support()
	//
	//! \brief   Description:  Gets which IDD features/operations are supported by the device
	//
	//  Entry:
	//!   \param[in] device = file descriptor
	//!   \param[in] iddSupport = pointer to a iddSupportedFeatures structure that will hold which features are supported
	//!
	//  Exit:
	//!   \return SUCCESS on successful completion, FAILURE = fail, NOT_SUPPORTED = IDD not supported
	//
	//-----------------------------------------------------------------------------
	OPENSEA_OPERATIONS_API int get_IDD_Support(tDevice *device, ptrIDDSupportedFeatures iddSupport);

	//-----------------------------------------------------------------------------
	//
	//  get_Approximate_IDD_Time()
	//
	//! \brief   Description:  Gets an approximate time for how long a specific IDD operation may take
	//
	//  Entry:
	//!   \param[in] device = file descriptor
	//!   \param[in] iddTest = enum value describing the IDD test to get the time for
	//!   \param[in] timeInSeconds = pointer to a uint64_t that will hold the amount of time in seconds that IDD is estimated to take
	//!
	//  Exit:
	//!   \return SUCCESS on successful completion, FAILURE = fail, NOT_SUPPORTED = IDD not supported
	//
	//-----------------------------------------------------------------------------
	OPENSEA_OPERATIONS_API int get_Approximate_IDD_Time(tDevice *device, eIDDTests iddTest, uint64_t *timeInSeconds);

	//-----------------------------------------------------------------------------
	//
	//  run_IDD()
	//
	//! \brief   Description:  Function to send a Seagate ATA IDD test to a device
	//
	//  Entry:
	//!   \param[in] device = file descriptor
	//!   \param[in] IDDtest = enum value describing the IDD test to run (CAPTIVE not supported right now)
	//!   \param[in] pollForProgress = 0 = don't poll, just start the test. 1 = poll for progress and display the progress on the screen.
	//!   \param[in] captive = set to true to force running the test in captive mode. Long test only!
	//!
	//  Exit:
	//!   \return SUCCESS on successful completion, FAILURE = fail
	//
	//-----------------------------------------------------------------------------
	OPENSEA_OPERATIONS_API int run_IDD(tDevice *device, eIDDTests IDDtest, bool pollForProgress, bool captive);

	OPENSEA_OPERATIONS_API int get_IDD_Status(tDevice *device, uint8_t *status);

#if defined (__cplusplus)
}
#endif