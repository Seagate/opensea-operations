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
// \file opensea_operation_version.h
// \brief Defines the versioning information for opensea-transport API


#pragma once

#if defined (__cplusplus)
extern "C"
{
#endif

#define COMBINE_OPERATION_VERSIONS_(x,y,z) #x "." #y "." #z
#define COMBINE_OPERATION_VERSIONS(x,y,z) COMBINE_OPERATION_VERSIONS_(x,y,z)

#define OPENSEA_OPERATION_MAJOR_VERSION 2
#define OPENSEA_OPERATION_MINOR_VERSION 0
#define OPENSEA_OPERATION_PATCH_VERSION 3

#define OPENSEA_OPERATION_VERSION COMBINE_OPERATION_VERSIONS(OPENSEA_OPERATION_MAJOR_VERSION,OPENSEA_OPERATION_MINOR_VERSION,OPENSEA_OPERATION_PATCH_VERSION)

#if defined (__cplusplus)
} //extern "C"
#endif

