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
// \file operations_Common.h
// \brief This file defines common things for the opensea-operations Library.

#pragma once

#include "common.h"
#include "common_public.h"
#include "ata_helper.h"
#include "ata_helper_func.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "cmds.h"

#if defined (__cplusplus)
#define __STDC_FORMAT_MACROS
extern "C"
{
#endif

    //This is a bunch of stuff for creating opensea-transport as a dynamic library (DLL in Windows or shared object in linux)
    #if defined(OPENSEA_OPERATIONS_API)
        #undef(OPENSEA_OPERATIONS_API)
    #endif
    
    #if defined(_WIN32) //DLL/LIB....be VERY careful making modifications to this unless you know what you are doing!
        #if defined (EXPORT_OPENSEA_OPERATIONS) && defined(STATIC_OPENSEA_OPERATIONS)
            #error "The preprocessor definitions EXPORT_OPENSEA_OPERATIONS and STATIC_OPENSEA_OPERATIONS cannot be combined!"
        #elif defined(STATIC_OPENSEA_OPERATIONS)
            #pragma message("Compiling opensea-operations as a static library!")
            #define OPENSEA_OPERATIONS_API
        #elif defined(EXPORT_OPENSEA_OPERATIONS)
            #pragma message("Compiling opensea-operations as exporting DLL!")
            #define OPENSEA_OPERATIONS_API __declspec(dllexport)
        #elif defined(IMPORT_OPENSEA_OPERATIONS)
            #pragma message("Compiling opensea-operations as importing DLL!")
            #define OPENSEA_OPERATIONS_API __declspec(dllimport)
        #else
            #error "You must specify STATIC_OPENSEA_OPERATIONS or EXPORT_OPENSEA_OPERATIONS or IMPORT_OPENSEA_OPERATIONS in the preprocessor definitions!"
        #endif
    #else //SO/A....as far as I know, nothing needs to be done here
        #define OPENSEA_OPERATIONS_API
    #endif
#if defined (__cplusplus)
}
#endif
