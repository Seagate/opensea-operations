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
// \file set_sector_size.h
// \brief This file defines the function calls for performing sector size changes (ATA and SCSI)

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  is_Set_Sector_Configuration_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if the device supports changing the sector size. On ATA, this checks if the set configuration ext command is supported. On SCSI, this checks for fast format support.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return true = changing sector size supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Set_Sector_Configuration_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  set_Sector_Configuration(tDevice *device, uint32_t sectorSize)
    //
    //! \brief   Description: Sends the command to quickly change the sector size. On ATA this is the set sector configuration command, on SAS, this is a fast format.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] sectorSize = sector size to change to.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully changed sector size, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_Sector_Configuration(tDevice *device, uint32_t sectorSize);

    //-----------------------------------------------------------------------------
    //
    //  show_Supported_Sector_Sizes(tDevice *device)
    //
    //! \brief   Description: Shows the sector sizes a device reports that is supports (if it supports telling you what is available)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = success showing sector sizes, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_Supported_Sector_Sizes(tDevice *device);

    typedef struct _sectorSize
    {
        bool valid;
        uint32_t logicalBlockLength;
        union
        {
            struct ataSetSectorFields
            {
                uint16_t descriptorCheck;//ATA
                uint8_t descriptorIndex;
            }ataSetSectorFields;
            struct scsiSectorBits //SCSI/SAS (Bits that describe how the device supports a logical block length)
            {
                bool p_i_i_sup;
                bool no_pi_chk;
                bool grd_chk;
                bool app_chk;
                bool ref_chk;
                bool t3ps;
                bool t2ps;
                bool t1ps;
                bool t0ps;
            }scsiSectorBits;
        };
    }sectorSize;

    uint32_t get_Number_Of_Supported_Sector_Sizes(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_Supported_Sector_Sizes(tDevice *device, sectorSize * ptrSectorSizeList, uint8_t numberOfSectorSizeStructs)
    //
    //! \brief   Description: Gets the sector sizes a device reports it supports changing to.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] ptrSectorSizeList = pointer to a list of sectorSize structs to fill
    //!   \param[in] numberOfSectorSizeStructs = number of sectorSize structs in list pointed to by ptrSectorSizeList. Recommend this is MAX_NUMBER_SUPPORTED_SECTOR_SIZES
    //!
    //  Exit:
    //!   \return SUCCESS = successfully got the reported sector sizes, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Supported_Sector_Sizes(tDevice *device, sectorSize *ptrSectorSizeList, uint32_t numberOfSectorSizeStructs);

    //-----------------------------------------------------------------------------
    //
    //  ata_Map_Sector_Size_To_Descriptor_Check(tDevice *device, uint32_t logicalBlockLength, uint16_t *descriptorCheckCode, uint8_t *descriptorIndex)
    //
    //! \brief   Description: Takes a sector size that is requested and maps it to a descriptor in the sector size log on ATA to set the correct parameters to use in the set sector configuration command.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logicalBlockLength = the logical block length you want to change to
    //!   \param[out] descriptorCheckCode = pointer to a word that will hold the descriptor check code to send to the device
    //!   \param[out] descriptorIndex = pointer to a byte that will hold the descriptor index to send to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully mapped sector size to a descriptor from the device, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Map_Sector_Size_To_Descriptor_Check(tDevice *device, uint32_t logicalBlockLength, uint16_t *descriptorCheckCode, uint8_t *descriptorIndex);

#if defined (__cplusplus)
}
#endif