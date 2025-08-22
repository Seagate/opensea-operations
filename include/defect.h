// SPDX-License-Identifier: MPL-2.0

//! \file defect.h
//! \brief Defines functions, enums, types, etc. for creating test defects and reading defect information from
//! ATA, SCSI, and NVMe storage devices
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "operations_Common.h"
#include "secure_file.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //! \struct blockFormatAddress
    //! \brief SCSI Defect block address format data.
    //! \note Uses an enum to switch between short and long formats
    //! \note Used as a substructure of \a scsiDefectList which contains an enum
    //! so that it is clear which version to parse out of here
    typedef struct s_blockFormatAddress // used by short and long descriptors
    {
        union
        {
            //! \var shortBlockAddress
            //! \brief 32bit block address of a defect
            uint32_t shortBlockAddress;
            //! \var longBlockAddress
            //! \brief 64bit block address of a defect
            uint64_t longBlockAddress;
        };
        //! \var pad
        //! \brief unused padding bytes to keep this structure
        //! allocated to the same length as other defect types.
        uint8_t pad[2];
    } blockFormatAddress;

    //! \struct bytesFromIndexAddress
    //! \brief SCSI bytes from index defect descriptor
    //! This is used for standard and extended formats.
    //! \note Used as a substructure of \a scsiDefectList which contains an enum
    //! so that it is clear which version to parse out of here
    typedef struct s_bytesFromIndexAddress // used by standard and extended
    {
        //! \var cylinderNumber
        //! \brief cylinder that the defect is on
        uint32_t cylinderNumber;
        //! \var headNumber
        //! \brief head that the defect is on
        uint8_t headNumber;
        //! \var bytesFromIndex
        //! \brief the number of bytes from the index to where the defect is at.
        uint32_t bytesFromIndex;
        //! \var multiAddressDescriptorStart
        //! \brief For extended bytes from index, this can be set to true to indicate this is the beginning
        //! of a defect that extends into another address. When this gets cleared to false again, then the
        //! range of the defect is between these descriptors.
        //! \note only for extended format. Always false in short bytes from index descriptors
        bool multiAddressDescriptorStart;
    } bytesFromIndexAddress;

    //! \struct physicalSectorAddress
    //! \brief SCSI Physical Cylinder-Head-Sector defect descriptor
    //! This is used for standard and extended formats.
    //! \note Used as a substructure of \a scsiDefectList which contains an enum
    //! so that it is clear which version to parse out of here
    typedef struct s_physicalSectorAddress
    {
        //! \var cylinderNumber
        //! \brief cylinder that the defect is on
        uint32_t cylinderNumber;
        //! \var headNumber
        //! \brief head that the defect is on
        uint8_t headNumber;
        //! \var sectorNumber
        //! \brief sector that the defect is on
        uint32_t sectorNumber;
        //! \var multiAddressDescriptorStart
        //! \brief For extended bytes from index, this can be set to true to indicate this is the beginning
        //! of a defect that extends into another address. When this gets cleared to false again, then the
        //! range of the defect is between these descriptors.
        //! \note only for extended format. Always false in short bytes from index descriptors
        bool multiAddressDescriptorStart;
    } physicalSectorAddress;

    //! \struct scsiDefectList
    //! \brief Output structure holding the requested SCSI defect list from \a get_SCSI_Defect_List()
    //! \details This list gives you the format, how many defects were reported, and if the list is
    //! primary defects (factory defects), grown defects (reallocations), or a combination of both.
    typedef struct s_scsiDefectList
    {
        //! \var format
        //! \see eSCSIAddressDescriptors in opensea-transport/scsi_helper.h
        //! \see SCSI Block Commands (SBC) to read more about the differences.
        //! \brief Specifies the format of the reported defects in the union at the end of this structure
        eSCSIAddressDescriptors format;
        //! \var numberOfElements
        //! \brief how many entries are stored in the defect list at the end of this structure using the
        //! \a format above.
        uint32_t numberOfElements;
        //! \var containsPrimaryList
        //! \brief If true, the list of defects includes the primary (factory) defect list
        bool containsPrimaryList;
        //! \var containsGrownList
        //! \brief If true, the list of defects includes the grown (reallocated) defect list
        bool containsGrownList;
        //! \var generation
        //! \brief The generation code of the defect list
        //! \note Drives only supporting the 10B read defect data command will set 0, which is an invalid value.
        //! Valid values are 1 - FFFFh. This number changes when new things are added to the list and it is read again.
        uint16_t generation;
        //! \var overflow
        //! \brief if the defect list is too long to read, this is set to true.
        //! \note If the defect list is too long for a single command or is larger than the OS supports reading in a
        //! single command, this may be set to true. Many newer drives may support reading with offsets, but may set
        //! this as well if an error is encountered trying to read the defect list.
        bool overflow;
        //! \var deviceHasMultipleLogicalUnits
        //! \brief Set to true when the device has multiple logical units (actuators) which may be in this list
        bool deviceHasMultipleLogicalUnits;
        union
        {
            //! \var block
            //! \brief list of reported block defects
            //! \see \a blockFormatAddress
            blockFormatAddress block[1];
            //! \var bfi
            //! \brief list of reported bytes from index defects
            //! \see \a bytesFromIndexAddress
            bytesFromIndexAddress bfi[1];
            //! \var physical
            //! \brief list of reported physical cylinder-head-sector defects
            //! \see \a physicalSectorAddress
            physicalSectorAddress physical[1];
        };
    } scsiDefectList, *ptrSCSIDefectList;

    //! \fn eReturnValues get_SCSI_Defect_List(tDevice*           device,
    //!                                                           eSCSIAddressDescriptors defectListFormat,
    //!                                                           bool                    grownList,
    //!                                                           bool                    primaryList,
    //!                                                           scsiDefectList**        defects)
    //! \brief Read a defect list from a SCSI device. Specify the requested format type and if the list
    //! should include the primary (factory) defect list and/or the grown (reallocated) defect list
    //! \note This function will allocate the defect list for you. Free it with \a free_Defect_List()
    //! \note Not all devices support all defect formats.
    //! \param[in] device pointer to the device structure with the device to read the defect list from
    //! \param[in] defectListFormat requested format of the defect list. See \a eSCSIAddressDescriptors
    //! \param[in] grownList set to true to include the grown defect list in the output
    //! \param[in] primaryList set to true to include the primary defect list in the output
    //! \param[out] defects pointer for the defect list. The list will be allocated for you.
    //! \code
    //! ptrSCSIDefectList defects = M_NULLPTR;
    //! result = get_SCSI_Defect_List(dev, format, true, false, &defects);
    //! ...do something with the list
    //! free_Defect_List(&defects);
    //! \endcode
    //! \return SUCCESS = successfully read the requested defect list. Other values may indicate an unsupported
    //! list or list format or that the device does not support returning the defect list. May fail if
    //! a failure occurs while trying to read the defect list
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Defect_List(tDevice*                device,
                                                              eSCSIAddressDescriptors defectListFormat,
                                                              bool                    grownList,
                                                              bool                    primaryList,
                                                              scsiDefectList**        defects);

    //! \fn void free_Defect_List(scsiDefectList** defects)
    //! \brief frees the SCSI defect list allocated by \a get_SCSI_Defect_List()
    //! \param[inout] defects double pointer to the defect list. Once free'd, this will be set to a NULL pointer
    //! \return void
    OPENSEA_OPERATIONS_API void free_Defect_List(scsiDefectList** defects);

    //! \fn void print_SCSI_Defect_List(ptrSCSIDefectList defects)
    //! \brief prints the defect list provided to stdout
    //! \param[in] defects pointer to the defect list to print out
    //! \return void
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API void print_SCSI_Defect_List(ptrSCSIDefectList defects);

    //! \fn eReturnValues create_Random_Uncorrectables(tDevice*      device,
    //!                                                              uint16_t      numberOfRandomLBAs,
    //!                                                              bool          readUncorrectables,
    //!                                                              bool          flaggedErrors,
    //!                                                              custom_Update updateFunction,
    //!                                                              void*         updateData)
    //! \brief Creates random psuedo uncorrectable or flagged uncorrectable errors on the drive.
    //! All errors are written to the full physical sector.
    //! \param[in] device pointer to the device structure with the device to create defects on
    //! \param[in] numberOfRandomLBAs how many defects to create
    //! \param[in] readUncorrectables if true, issues a read to the defect after writing. By reading the defect,
    //! this ensures it is logged into the device's defect list (for psuedo uncorrectables)
    //! \param[in] flaggedErrors if true create the defect using the flagged defect method. This marks a sector as
    //! having a defect without logging it in the device's pending defect list
    //! \param updateFunction unused
    //! \param updateData unused
    //! \return SUCCESS if defects successfully created otherwise an error code for the failure.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues create_Random_Uncorrectables(tDevice*      device,
                                                                      uint16_t      numberOfRandomLBAs,
                                                                      bool          readUncorrectables,
                                                                      bool          flaggedErrors,
                                                                      custom_Update updateFunction,
                                                                      void*         updateData);

    //! \fn eReturnValues create_Uncorrectables(tDevice*      device,
    //!                                                       uint64_t      startingLBA,
    //!                                                       uint64_t      range,
    //!                                                       bool          readUncorrectables,
    //!                                                       custom_Update updateFunction,
    //!                                                       void*         updateData)
    //! \brief Creates psuedo uncorrectable defects on the drive for the specified
    //! starting LBA and range. All errors are written to the full physical sector.
    //! \param[in] device pointer to the device structure with the device to create defects on
    //! \param[in] startingLBA First LBA to create a defect at
    //! \param[in] range Number of LBAs to create a defect on
    //! \param[in] readUncorrectables if true, issues a read to the defect after writing. By reading the defect,
    //! this ensures it is logged into the device's defect list (for psuedo uncorrectables)
    //! \param updateFunction unused
    //! \param updateData unused
    //! \return SUCCESS if defects successfully created otherwise an error code for the failure.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues create_Uncorrectables(tDevice*      device,
                                                               uint64_t      startingLBA,
                                                               uint64_t      range,
                                                               bool          readUncorrectables,
                                                               custom_Update updateFunction,
                                                               void*         updateData);

    //! \fn eReturnValues flag_Uncorrectables(tDevice*        device,
    //!                                                       uint64_t      startingLBA,
    //!                                                       uint64_t      range,
    //!                                                       custom_Update updateFunction,
    //!                                                       void*         updateData)
    //! \brief Creates flagged uncorrectable defects on the drive for the specified
    //! starting LBA and range. All errors are written to the full physical sector.
    //! \param[in] device pointer to the device structure with the device to create defects on
    //! \param[in] startingLBA First LBA to create a defect at
    //! \param[in] range Number of LBAs to create a defect on
    //! \param updateFunction unused
    //! \param updateData unused
    //! \return SUCCESS if defects successfully created otherwise an error code for the failure.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues flag_Uncorrectables(tDevice*      device,
                                                             uint64_t      startingLBA,
                                                             uint64_t      range,
                                                             custom_Update updateFunction,
                                                             void*         updateData);

    //! \fn bool is_Read_Long_Write_Long_Supported(tDevice* device)
    //! \brief Checks if the legacy read long/write long commands are supported for creating errors
    //! \details These commands are obsolete and have been for years, but a device may still support them.
    //! These work by reading the sector data and ECC data to the host. Then it can be modified and written
    //! back using the write long command. This allows for testing of error correction capabilities or
    //! marking a sector with an uncorrectable defect.
    //! \param[in] device pointer to the device structure
    //! \return true = supported, false = not supported
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_Read_Long_Write_Long_Supported(tDevice* device);

    //! \fn eReturnValues corrupt_LBA_Read_Write_Long(tDevice* device,
    //!                                               uint64_t corruptLBA,
    //!                                               uint16_t numberOfBytesToCorrupt)
    //! \brief Uses the read long/write long commands to modify a single physical sector
    //! with the specified number of byte to create either a correctable or uncorrectable defect.
    //! \note correctable vs uncorrectable defect depends on how many bytes can be corrupted before the ECC
    //! algorithm is no longer capable of performing error correction for the data.
    //! \param[in] device pointer to the device structure
    //! \param[in] corruptLBA the LBA address to write the modification to
    //! \param[in] numberOfBytesToCorrupt how many bytes in the physical sector to modify
    //! \return SUCCESS if the modification worked. NOT_SUPPORTED if the device does not support these commands,
    //! any other error for a failure may be returned.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues corrupt_LBA_Read_Write_Long(tDevice* device,
                                                                     uint64_t corruptLBA,
                                                                     uint16_t numberOfBytesToCorrupt);

    //! \fn eReturnValues corrupt_LBAs(tDevice* device,
    //!                                uint64_t      startingLBA,
    //!                                uint64_t      range,
    //!                                bool          readCorruptedLBAs,
    //!                                uint16_t      numberOfBytesToCorrupt,
    //!                                custom_Update updateFunction,
    //!                                void*         updateData)
    //! \brief Uses the read long/write long commands to modify a multiple physical sectors
    //! with the specified number of byte to create either a correctable or uncorrectable defects.
    //! \note correctable vs uncorrectable defect depends on how many bytes can be corrupted before the ECC
    //! algorithm is no longer capable of performing error correction for the data.
    //! \param[in] device pointer to the device structure
    //! \param[in] startingLBA First LBA to create a defect at
    //! \param[in] range Number of LBAs to create a defect on
    //! \param[in] readCorruptedLBAs if true, will issue a read command to the corrupted LBAs. If the error is
    //! uncorrectable, the device will log it to its pending defect list.
    //! \param[in] numberOfBytesToCorrupt how many bytes in the physical sector to modify
    //! \param updateFunction unused
    //! \param updateData unused
    //! \return SUCCESS if the modification worked. NOT_SUPPORTED if the device does not support these commands,
    //! any other error for a failure may be returned.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues corrupt_LBAs(tDevice*      device,
                                                      uint64_t      startingLBA,
                                                      uint64_t      range,
                                                      bool          readCorruptedLBAs,
                                                      uint16_t      numberOfBytesToCorrupt,
                                                      custom_Update updateFunction,
                                                      void*         updateData);

    //! \fn eReturnValues corrupt_Random_LBAs(tDevice* device,
    //!                                       uint16_t      numberOfRandomLBAs,
    //!                                       bool          readCorruptedLBAs,
    //!                                       uint16_t      numberOfBytesToCorrupt,
    //!                                       custom_Update updateFunction,
    //!                                       void*         updateData)
    //! \brief Uses the read long/write long commands to modify a multiple physical sectors
    //! with the specified number of byte to create either a correctable or uncorrectable defects.
    //! The sectors are chosen randomly.
    //! \note correctable vs uncorrectable defect depends on how many bytes can be corrupted before the ECC
    //! algorithm is no longer capable of performing error correction for the data.
    //! \param[in] device pointer to the device structure
    //! \param[in] numberOfRandomLBAs Number of randomly chosen LBAs to corrupt
    //! \param[in] readCorruptedLBAs if true, will issue a read command to the corrupted LBAs. If the error is
    //! uncorrectable, the device will log it to its pending defect list.
    //! \param[in] numberOfBytesToCorrupt how many bytes in the physical sector to modify
    //! \param updateFunction unused
    //! \param updateData unused
    //! \return SUCCESS if the modification worked. NOT_SUPPORTED if the device does not support these commands,
    //! any other error for a failure may be returned.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues corrupt_Random_LBAs(tDevice*      device,
                                                             uint16_t      numberOfRandomLBAs,
                                                             bool          readCorruptedLBAs,
                                                             uint16_t      numberOfBytesToCorrupt,
                                                             custom_Update updateFunction,
                                                             void*         updateData);

    //! \struct pendingDefect
    //! \brief Holds the LBA with a pending defect and the power on hours at which
    //! the defect was added to the pending defect list.
    typedef struct s_pendingDefect
    {
        //! \var powerOnHours
        //! \brief number of power on hours at which the defect was found
        uint32_t powerOnHours;
        //! \var lba
        //! \brief logical block address of the pending defect
        uint64_t lba;
    } pendingDefect, *ptrPendingDefect;

    //! \fn void safe_free_pending_defect(pendingDefect** defect)
    //! \brief helper function to safely free and set to null the defect list when
    //! it is done being used.
    static M_INLINE void safe_free_pending_defect(pendingDefect** defect)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, defect));
    }

    //! \def MAX_PLIST_ENTRIES
    //! \brief maximum number of reportable pending defects
    //! \note Using ACS standard maximum reportable count
#define MAX_PLIST_ENTRIES UINT16_C(65534)

    //! \fn eReturnValues get_LBAs_From_ATA_Pending_List(tDevice*         device,
    //!                                                                   ptrPendingDefect defectList,
    //!                                                                   uint32_t*        numberOfDefects)
    //! \brief Reads the pending defect list from an ATA drive if the list is supported
    //! \param[in] device pointer to device structure for the device to access
    //! \param[out] defectList pointer to the defect list which will be filled in. Should be allocated with
    //! space for \a MAX_PLIST_ENTRIES
    //! \param[out] numberOfDefects will be set to the number of defects actually read from the device's list
    //! \return SUCCESS = successfully read the pending defect list. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read pending defect list.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_LBAs_From_ATA_Pending_List(tDevice*         device,
                                                                        ptrPendingDefect defectList,
                                                                        uint32_t*        numberOfDefects);

    //! \fn eReturnValues get_LBAs_From_SCSI_Pending_List(tDevice*         device,
    //!                                                                    ptrPendingDefect defectList,
    //!                                                                    uint32_t*        numberOfDefects)
    //! \brief Reads the pending defect list from an SCSI drive if the list is supported
    //! \param[in] device pointer to device structure for the device to access
    //! \param[out] defectList pointer to the defect list which will be filled in. Should be allocated with
    //! space for \a MAX_PLIST_ENTRIES
    //! \param[out] numberOfDefects will be set to the number of defects actually read from the device's list
    //! \return SUCCESS = successfully read the pending defect list. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read pending defect list.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_LBAs_From_SCSI_Pending_List(tDevice*         device,
                                                                         ptrPendingDefect defectList,
                                                                         uint32_t*        numberOfDefects);

    //! \fn eReturnValues get_LBAs_From_Pending_List(tDevice*         device,
    //!                                                               ptrPendingDefect defectList,
    //!                                                               uint32_t*        numberOfDefects)
    //! \brief Reads the pending defect list from a SCSI or ATA drive if the list is supported
    //! \param[in] device pointer to device structure for the device to access
    //! \param[out] defectList pointer to the defect list which will be filled in. Should be allocated with
    //! space for \a MAX_PLIST_ENTRIES
    //! \param[out] numberOfDefects will be set to the number of defects actually read from the device's list
    //! \return SUCCESS = successfully read the pending defect list. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read pending defect list.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_LBAs_From_Pending_List(tDevice*         device,
                                                                    ptrPendingDefect defectList,
                                                                    uint32_t*        numberOfDefects);

    //! \fn void show_Pending_List(ptrPendingDefect pendingList, uint32_t numberOfItemsInPendingList)
    //! \brief writes the provided pending defect list to stdout
    //! \param[in] pendingList pointer to the pending list
    //! \param[in] numberOfItemsInPendingList number of defects in the \a pendingList
    //! \return void
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API void show_Pending_List(ptrPendingDefect pendingList, uint32_t numberOfItemsInPendingList);

    //! \fn eReturnValues get_LBAs_From_DST_Log(tDevice*         device,
    //!                                                          ptrPendingDefect defectList,
    //!                                                          uint32_t*        numberOfDefects)
    //! \brief reads a list of LBAs logged as read failures in the device self-test log
    //! \param[in] device pointer to device structure for the device to access
    //! \param[out] defectList pointer to the defect list which will be filled in. Should be allocated with
    //! space for at least \a MAX_DST_ENTRIES (in dst.h)
    //! \param[out] numberOfDefects will be set to the number of defects actually read from the device's list
    //! \return SUCCESS = successfully read the dst log. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read dst log
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_LBAs_From_DST_Log(tDevice*         device,
                                                               ptrPendingDefect defectList,
                                                               uint32_t*        numberOfDefects);

//! \def MAX_BACKGROUND_SCAN_RESULTS
//! \brief the maximum number of background scan results supported on a SCSI device
#define MAX_BACKGROUND_SCAN_RESULTS UINT32_C(2048) // parameter codes 1 - 800h

    //! \struct backgroundResults
    //! \brief Structure of an individual background scan result from the SCSI background scan results log page
    typedef struct s_backgroundResults
    {
        //! \var reassignStatus
        //! \brief scsi reassign status value. Use this to know if it's been reassigned or not
        //! \see SBC specification for details on this value. TODO: Add enum for status's
        uint8_t reassignStatus;
        //! \var accumulatedPowerOnMinutes
        //! \brief Number of power on minutes when this was logged.
        uint64_t accumulatedPowerOnMinutes;
        //! \var senseKey
        //! \see SPC
        uint8_t senseKey;
        //! \var additionalSenseCode
        //! \see SPC
        uint8_t additionalSenseCode;
        //! \var additionalSenseCodeQualifier
        //! \see SPC
        uint8_t additionalSenseCodeQualifier;
        //! \var lba
        //! \brief logical block associated with this background scan result
        uint64_t lba;
    } backgroundResults, *ptrBackgroundResults;

    //! \fn void safe_free_background_results(backgroundResults** bg)
    //! \brief helper function to free the background scan results list
    static M_INLINE void safe_free_background_results(backgroundResults** bg)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, bg));
    }

    //! \fn eReturnValues get_SCSI_Background_Scan_Results(tDevice*             device,
    //!                                                                         ptrBackgroundResults results,
    //!                                                                         uint16_t*            numberOfResults)
    //! \brief Reads the SCSI background scan results log into a list
    //! \param[in] device pointer to device structure for the device to access
    //! \param[out] results pointer to results which should be allocated for \a MAX_BACKGROUND_SCAN_RESULTS
    //! entries that can be read from the device
    //! \param[out] numberOfResults holds the number of results read from the device
    //! \return SUCCESS = successfully read the bms log. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read bms log
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Background_Scan_Results(tDevice*             device,
                                                                          ptrBackgroundResults results,
                                                                          uint16_t*            numberOfResults);

    //! \fn eReturnValues get_LBAs_From_SCSI_Background_Scan_Log(tDevice*         device,
    //!                                                                           ptrPendingDefect defectList,
    //!                                                                           uint32_t*        numberOfDefects)
    //! \brief Reads a list of LBAs from the background scan results log to review for additional defects
    //! \details This does not filter based on reassign status. This just gets a list of LBAs to review/read around
    //! for additional defects
    //! \param[out] defectList pointer to defect list that should be \a MAX_BACKGROUND_SCAN_RESULTS in size
    //! \param[out] numberOfResults holds the number of defects read from the device
    //! \return SUCCESS = successfully read the bms log. NOT_SUPPORTED = log not supported by the device,
    //! any other error = failure to read bms log
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_LBAs_From_SCSI_Background_Scan_Log(tDevice*         device,
                                                                                ptrPendingDefect defectList,
                                                                                uint32_t*        numberOfDefects);

#if defined(__cplusplus)
}
#endif
