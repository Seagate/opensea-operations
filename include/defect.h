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
// \file defect.h
// \brief This file defines the functions for creating and reading defect information

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

    typedef struct _blockFormatAddress //used by short and long descriptors
    {
        union
        {
            uint32_t shortBlockAddress;
            uint64_t longBlockAddress;
        };
        uint8_t pad[2];//extra pad bytes for union below
    }blockFormatAddress;

    typedef struct _bytesFromIndexAddress //used by standard and extended
    {
        uint32_t cylinderNumber;
        uint8_t headNumber;
        uint32_t bytesFromIndex;
        bool multiAddressDescriptorStart;//only valid in extended bytes from index address format
    }bytesFromIndexAddress;

    typedef struct _physicalSectorAddress
    {
        uint32_t cylinderNumber;
        uint8_t headNumber;
        uint32_t sectorNumber;
        bool multiAddressDescriptorStart;//only valid in extended physical sector address format
    }physicalSectorAddress;

    typedef struct _scsiDefectList
    {
        eSCSIAddressDescriptors format;
        uint32_t numberOfElements;//number of things stored in the list in the format mentioned above
        bool containsPrimaryList;
        bool containsGrownList;
        uint16_t generation;//0 is an invalid or not supported generation code. Should be 1 through FFFFh
        bool overflow;//This will be set if the defect list length is so long it cannot be read entirely, or if it can only be read in one command and is too big of a transfer for the host to handle. (>128k in size for Windows)
        union
        {
            blockFormatAddress block[1];
            bytesFromIndexAddress bfi[1];
            physicalSectorAddress physical[1];
        };
        bool deviceHasMultipleLogicalUnits;
    }scsiDefectList, *ptrSCSIDefectList;

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Defect_List(tDevice *device, eSCSIAddressDescriptors defectListFormat, bool grownList, bool primaryList, scsiDefectList **defects)
    //
    //! \brief   Description:  Use this function to read SCSI Primary and Grown defects. This function will allocate teh defect list for you!
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] defectListFormat = format to pull the defect list in
    //!   \param[in] grownList = requests the grown (reallocated) defects list
    //!   \param[in] primaryList = requests the primary (factory) defects list
    //!   \param[in] defects = This will hold a list of the defects reported by the device. This will be allocated for you in this function.
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SCSI_Defect_List(tDevice *device, eSCSIAddressDescriptors defectListFormat, bool grownList, bool primaryList, scsiDefectList **defects);

    //-----------------------------------------------------------------------------
    //
    //  free_Defect_List(scsiDefectList **defects)
    //
    //! \brief   Description:  Frees the defect list allocated for you in get_SCSI_Defect_List()
    //
    //  Entry:
    //!   \param[in] defects = The defect list to free
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void free_Defect_List(scsiDefectList **defects);

    //-----------------------------------------------------------------------------
    //
    //  print_SCSI_Defect_List(scsiDefectList **defects)
    //
    //! \brief   Description: Prints the defect list given to the screen
    //
    //  Entry:
    //!   \param[in] defects = The defect list to print
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_SCSI_Defect_List(ptrSCSIDefectList defects);

    //-----------------------------------------------------------------------------
    //
    //  create_Random_Uncorrectables()
    //
    //! \brief   Description:  This function creates random uncorrectable errors on the drive. All errors created are written to the entire physical sector of the drive. If the read flag is not set to true, these errors may not end up being logged in the Pending Defect list
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] numberOfRandomLBAs = number of random errors to create
    //!   \param[in] readUncorrectables = set to true to read the lba after marking it bad with a psuedo uncorrectable error (recommended so it can be logged and tracked)
    //!   \param[in] flaggedErrors = set to true to flag uncorrectable errors instead of creating pseudo uncorrectable errors. (Required on NVMe). Note: These errors cannot be logged. Use with caution!!!
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int create_Random_Uncorrectables(tDevice *device, uint16_t numberOfRandomLBAs, bool readUncorrectables, bool flaggedErrors, custom_Update updateFunction, void *updateData);

    //-----------------------------------------------------------------------------
    //
    //  create_Uncorrectables()
    //
    //! \brief   Description:  This function creates a range of uncorrectable errors on the drive. All errors created are written to the entire physical sector of the drive, so if it's a 512/4k drive and the range specified is 16, this will create an error for 8 LBAs at LBA 1000 and 1008. If the read flag is not set to true, these errors may not end up being logged in the Pending Defect list
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start writing errors at
    //!   \param[in] range = number of LBAs after the starting LBA to write errors to
    //!   \param[in] readUncorrectables = Flag to specify whether or not to issue read commands to the created error. This should always be set to true unless you know what you're doing
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int create_Uncorrectables(tDevice *device, uint64_t startingLBA, uint64_t range, bool readUncorrectables, custom_Update updateFunction, void *updateData);

    //-----------------------------------------------------------------------------
    //
    //  flag_Uncorrectables()
    //
    //! \brief   Description:  This function creates a range of flagged uncorrectable errors on the drive. All errors created are written to the entire physical sector of the drive, so if it's a 512/4k drive and the range specified is 16, this will create an error for 8 LBAs at LBA 1000 and 1008.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start writing errors at
    //!   \param[in] range = number of LBAs after the starting LBA to write errors to
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int flag_Uncorrectables(tDevice *device, uint64_t startingLBA, uint64_t range, custom_Update updateFunction, void *updateData);

    //-----------------------------------------------------------------------------
    //
    //  is_Read_Long_Write_Long_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if a drive supports using read long and write long commands to create errors on a drive. (This is obsolete on new drives)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Read_Long_Write_Long_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  corrupt_LBA_Read_Write_Long(tDevice *device, uint64_t corruptLBA, uint16_t numberOfBytesToCorrupt)
    //
    //! \brief   Description:  Performs a read long, modify, write long on a drive to a single (physical) sector to create an error condition
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] corruptLBA = LBA to corrupt with an error
    //!   \param[in] numberOfBytesToCorrupt = This is the number of bytes to corrupt in the user-data to test the ECC algorithm. If set to zero, no changes are made. A value greater than the sector size will corrupt all data bytes. Otherwise, only the number of specified bytes will be changed to create the error (correctable or uncorrectable)
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int corrupt_LBA_Read_Write_Long(tDevice *device, uint64_t corruptLBA, uint16_t numberOfBytesToCorrupt);

    //-----------------------------------------------------------------------------
    //
    //  corrupt_LBAs(tDevice *device, uint64_t startingLBA, uint64_t range, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, custom_Update updateFunction, void *updateData)
    //
    //! \brief   Description:  Uses the corrupt_LBA_Read_Write_Long function to corrupt a range of LBAs
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = 1st LBA to corrupt with an error
    //!   \param[in] range = # of LBAs after the first to corrupt with an error
    //!   \param[in] readCorruptedLBAs = set to true (recommended) to perform a read to the LBA so that if it is uncorrectable, it can be logged by the drive
    //!   \param[in] numberOfBytesToCorrupt = This is the number of bytes to corrupt in the user-data to test the ECC algorithm. If set to zero, no changes are made. A value greater than the sector size will corrupt all data bytes. Otherwise, only the number of specified bytes will be changed to create the error (correctable or uncorrectable)
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int corrupt_LBAs(tDevice *device, uint64_t startingLBA, uint64_t range, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, custom_Update updateFunction, void *updateData);

    //-----------------------------------------------------------------------------
    //
    //  corrupt_Random_LBAs(tDevice *device, uint16_t numberOfRandomLBAs, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, custom_Update updateFunction, void *updateData)
    //
    //! \brief   Description:  Uses the corrupt_LBA_Read_Write_Long function to corrupt random LBAs
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] numberOfRandomLBAs = 1st LBA to corrupt with an error
    //!   \param[in] readCorruptedLBAs = set to true (recommended) to perform a read to the LBA so that if it is uncorrectable, it can be logged by the drive
    //!   \param[in] numberOfBytesToCorrupt = This is the number of bytes to corrupt in the user-data to test the ECC algorithm. If set to zero, no changes are made. A value greater than the sector size will corrupt all data bytes. Otherwise, only the number of specified bytes will be changed to create the error (correctable or uncorrectable)
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int corrupt_Random_LBAs(tDevice *device, uint16_t numberOfRandomLBAs, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, custom_Update updateFunction, void *updateData);

#if defined(__cplusplus)
}
#endif
