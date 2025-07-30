// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file depopulate.h
// \brief This file defines the functions for depopulating physical/storage elements on a drive (Remanufacture)

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  is_Depopulation_Feature_Supported(tDevice *device, uint64_t *depopulationTime)
    //
    //! \brief   Description: Check if the depopulate feature is supported
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] depopulationTime = optional. Will hold an approximate time to perform a depopulate
    //!
    //  Exit:
    //!   \return true = depopulate supported, false = not supported.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API bool is_Depopulation_Feature_Supported(tDevice* device, uint64_t* depopulationTime);

    //-----------------------------------------------------------------------------
    //
    //  get_Number_Of_Descriptors(tDevice *device, uint32_t *numberOfDescriptors)
    //
    //! \brief   Description: Get the number of physical element descriptors supported, to allocate memory before
    //! reading them
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] numberOfDescriptors = holds a number of physical element descriptors, so that the right amount of
    //!   memory can be allocated before reading them
    //!
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_Number_Of_Descriptors(tDevice* device, uint32_t* numberOfDescriptors);

    typedef enum ePhysicalElementTypeEnum
    {
        PHYSICAL_ELEMENT_RESERVED        = 0,
        PHYSICAL_ELEMENT_STORAGE_ELEMENT = 1,
    } ePhysicalElementType;

    typedef struct s_physicalElement
    {
        uint32_t             elementIdentifier;
        ePhysicalElementType elementType;
        uint8_t              elementHealth;
        uint64_t             associatedCapacity;
        bool restorationAllowed; // can run the Restore elements and rebuild and this element will return to use.
    } physicalElement, *ptrPhysicalElement;

    static M_INLINE void safe_free_physical_element(physicalElement** pe)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, pe));
    }

    //-----------------------------------------------------------------------------
    //
    //  get_Physical_Element_Descriptors(tDevice *device, uint32_t numberOfElementsExpected, ptrPhysicalElement
    //  elementList)
    //
    //! \brief   Description: Get the physical element descriptors from a drive
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] numberOfElementsExpected = number of physical element descriptors expected to be read and number
    //!   allocated to read \param[out] elementList = pointer to the element list that holds each of the physical
    //!   element descriptors
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Physical_Element_Descriptors(tDevice*           device,
                                                                          uint32_t           numberOfElementsExpected,
                                                                          ptrPhysicalElement elementList);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(3)
    M_PARAM_RW(4)
    M_PARAM_RW(5)
    M_PARAM_WO(6)
    OPENSEA_OPERATIONS_API eReturnValues get_Physical_Element_Descriptors_2(tDevice*  device,
                                                                            uint32_t  numberOfElementsExpected,
                                                                            uint32_t* depopElementID,
                                                                            uint16_t* maximumDepopulatedElements,
                                                                            uint16_t* currentDepopulatedElements,
                                                                            ptrPhysicalElement elementList);

    //-----------------------------------------------------------------------------
    //
    //  show_Physical_Element_Descriptors(uint32_t numberOfElements, ptrPhysicalElement elementList, uint64_t
    //  depopulateTime)
    //
    //! \brief   Description: Show the physical element descriptors from a drive on the screen
    //
    //  Entry:
    //!   \param[in] numberOfElements = number of physical element descriptors expected to be read and number allocated
    //!   to read \param[in] elementList = pointer to the element list that holds each of the physical element
    //!   descriptors \param[in] depopulateTime = time to perform a depopulate. Will be displayed on the screen by this
    //!   function
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(2)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API void show_Physical_Element_Descriptors(uint32_t           numberOfElements,
                                                                  ptrPhysicalElement elementList,
                                                                  uint64_t           depopulateTime);

    M_NONNULL_PARAM_LIST(2)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API void show_Physical_Element_Descriptors_2(uint32_t           numberOfElements,
                                                                    ptrPhysicalElement elementList,
                                                                    uint64_t           depopulateTime,
                                                                    uint32_t           depopElementID,
                                                                    uint16_t           maximumDepopulatedElements,
                                                                    uint16_t           currentDepopulatedElements);

    //-----------------------------------------------------------------------------
    //
    //  depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA)
    //
    //! \brief   Description: Call this function to depopulate a physical element from use, optionally requesting a new
    //! max LBA
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] elementDescriptorID = descriptor ID of the element to depopulate
    //!   \param[in] requestedMaxLBA = If zero, the drive will decide a new max. Otherwise this value will be used
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues depopulate_Physical_Element(tDevice* device,
                                                                     uint32_t elementDescriptorID,
                                                                     uint64_t requestedMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  is_Repopulate_Feature_Supported(tDevice *device, uint64_t *depopulationTime)
    //
    //! \brief   Description: Check if the Restore elements and rebuild commands are supported. Depopulation time is
    //! reported for time estimate if the pointer is valid
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] depopulationTime = optional. Will hold an approximate time to perform a depopulate.
    //!
    //  Exit:
    //!   \return true = repopulate supported, false = not supported.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API bool is_Repopulate_Feature_Supported(tDevice* device, uint64_t* depopulationTime);

    //-----------------------------------------------------------------------------
    //
    //  repopulate_Elements(tDevice *device)
    //
    //! \brief   Description: Call this function to repopulate (Restore elements and rebuild). NOTE: At least one
    //! element must be rebuildable or this will return an error
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues repopulate_Elements(tDevice* device);

    typedef enum eDepopStatusEnum
    {
        DEPOP_NOT_IN_PROGRESS,
        DEPOP_IN_PROGRESS,
        DEPOP_REPOP_IN_PROGRESS,
        DEPOP_FAILED,
        DEPOP_REPOP_FAILED,
        DEPOP_INVALID_FIELD,
        DEPOP_MICROCODE_NEEDS_ACTIVATION
    } eDepopStatus;

    //-----------------------------------------------------------------------------
    //
    //  get_Depopulate_Progress(tDevice *device, eDepopStatus *depopStatus, double *progress)
    //
    //! \brief   Description: Returns the depopulate status and progress info. Also used for repopulate.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] depopStatus = pointer to the depopulate status enum to know what is going on. [Required]
    //!   \param[out] progress = percentage completed progress. NOTE: This is only available on SAS drives. If a
    //!   progress > 100 is returned, it is invalid. SATA will return 255% complete to indicate progress is not
    //!   available.
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Depopulate_Progress(tDevice*      device,
                                                                 eDepopStatus* depopStatus,
                                                                 double*       progress);

    //-----------------------------------------------------------------------------
    //
    //  show_Depop_Repop_Progress(tDevice *device)
    //
    //! \brief   Description: Gets and shows the depop or repop status and progress to stdout
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues show_Depop_Repop_Progress(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  perform_Depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA,
    //  bool pollForProgress)
    //
    //! \brief   Description: This function performs a full proccess of starting depopulation and checks to see if
    //! command was accepted or not and reasons for failure. Will also poll for progress until completed if specified.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] elementDescriptorID = descriptor ID of the element to depopulate
    //!   \param[in] requestedMaxLBA = a max LBA requested for the drive after an element has been depopulated. This may
    //!   or may not be accepted by the drive. If unsure, use zero to let the drive decide. \param[in] pollForProgress =
    //!   when set to true, this function will poll for progress and update the screen while still running.
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues perform_Depopulate_Physical_Element(tDevice* device,
                                                                             uint32_t elementDescriptorID,
                                                                             uint64_t requestedMaxLBA,
                                                                             bool     pollForProgress);

    //! \fn eReturnValues perform_Depopulate_Physical_Element2(tDevice* device,
    //!                                                        uint32_t elementDescriptorID,
    //!                                                        uint64_t requestedMaxLBA,
    //!                                                        bool     pollForProgress,
    //!                                                        bool     modifyZones)
    //! \brief Runs the depopulate physical element or depopulate and modify zones command and checks for failures.
    //! This function can also poll until complete when requested.
    //! \param[in] device pointer to the device structure to use when issuing the command
    //! \param[in] elementDescriptorID the physical element descriptor ID from get physical element status command
    //!            to remove/depopulate
    //! \param[in] requestedMaxLBA if nonzero, this value is passed in during the remove and truncate command
    //!                            to set this as the new max LBA of the drive. If set to zero, the drive will
    //!                            set the maxLBA to the highest possible value after depopulating the element.
    //! \param[in] pollForProgress if true, this function will poll for progress until the operation is finished.
    //! \param[in] modifyZones if true for a ZAC drive, this will run the remove and modify zones command instead of
    //!                        remove and truncate.
    //! \return SUCCESS = operation completed successfully. Any other code describes and error condition while running.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues perform_Depopulate_Physical_Element2(tDevice* device,
                                                                              uint32_t elementDescriptorID,
                                                                              uint64_t requestedMaxLBA,
                                                                              bool     pollForProgress,
                                                                              bool     modifyZones);

    //-----------------------------------------------------------------------------
    //
    //  perform_Repopulate_Physical_Element(tDevice *device, bool pollForProgress)
    //
    //! \brief   Description: This function performs a full start of repopulation and check to see if command was
    //! accepted or not and reasons for failure. Will also poll for progress until completed if specified.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] pollForProgress = when set to true, this function will poll for progress and update the screen
    //!   while still running.
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues perform_Repopulate_Physical_Element(tDevice* device, bool pollForProgress);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API bool is_Depopulate_And_Modify_Zones_Supported(tDevice* device, uint64_t* depopulationTime);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues depopulate_Physical_Element_And_Modify_Zones(tDevice* device,
                                                                                      uint32_t elementDescriptorID);

#if defined(__cplusplus)
}
#endif
