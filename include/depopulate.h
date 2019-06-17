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
// \file depopulate.h
// \brief This file defines the functions for depopulating physical/storage elements on a drive (Remanufacture)

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C" {
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
    OPENSEA_OPERATIONS_API bool is_Depopulation_Feature_Supported(tDevice *device, uint64_t *depopulationTime);

    //-----------------------------------------------------------------------------
    //
    //  get_Number_Of_Descriptors(tDevice *device, uint32_t *numberOfDescriptors)
    //
    //! \brief   Description: Get the number of physical element descriptors supported, to allocate memory before reading them
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] numberOfDescriptors = holds a number of physical element descriptors, so that the right amount of memory can be allocated before reading them
    //!
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Number_Of_Descriptors(tDevice *device, uint32_t *numberOfDescriptors);

    typedef enum _ePhysicalElementType
    {
        PHYSICAL_ELEMENT_RESERVED = 0,
        PHYSICAL_ELEMENT_STORAGE_ELEMENT = 1,
    }ePhysicalElementType;

    typedef struct _physicalElement
    {
        uint32_t elementIdentifier;
        ePhysicalElementType elementType;
        uint8_t elementHealth;
        uint64_t associatedCapacity;
    }physicalElement, *ptrPhysicalElement;

    //-----------------------------------------------------------------------------
    //
    //  get_Physical_Element_Descriptors(tDevice *device, uint32_t numberOfElementsExpected, ptrPhysicalElement elementList)
    //
    //! \brief   Description: Get the physical element descriptors from a drive
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] numberOfElementsExpected = number of physical element descriptors expected to be read and number allocated to read
    //!   \param[out] elementList = pointer to the element list that holds each of the physical element descriptors
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Physical_Element_Descriptors(tDevice *device, uint32_t numberOfElementsExpected, ptrPhysicalElement elementList);

    //-----------------------------------------------------------------------------
    //
    //  show_Physical_Element_Descriptors(uint32_t numberOfElements, ptrPhysicalElement elementList, uint64_t depopulateTime)
    //
    //! \brief   Description: Show the physical element descriptors from a drive on the screen
    //
    //  Entry:
    //!   \param[in] numberOfElements = number of physical element descriptors expected to be read and number allocated to read
    //!   \param[in] elementList = pointer to the element list that holds each of the physical element descriptors
    //!   \param[in] depopulateTime = time to perform a depopulate. Will be displayed on the screen by this function
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_Physical_Element_Descriptors(uint32_t numberOfElements, ptrPhysicalElement elementList, uint64_t depopulateTime);

    //-----------------------------------------------------------------------------
    //
    //  depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA)
    //
    //! \brief   Description: Call this function to depopulate a physical element from use, optionally requesting a new max LBA
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] elementDescriptorID = descriptor ID of the element to depopulate
    //!   \param[in] requestedMaxLBA = If zero, the drive will decide a new max. Otherwise this value will be used
    //  Exit:
    //!   \return SUCCESS = success, !SUCCESS = see error code, something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA);

#if defined(__cplusplus)
}
#endif