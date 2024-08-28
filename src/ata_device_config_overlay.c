// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2023-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file ata_device_config_overlay.c   ATA Device configuration overlay support (DCO)

#include "common_types.h"
#include "precision_timer.h"
#include "memory_safety.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "error_translation.h"
#include "io_utils.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "ata_device_config_overlay.h"

//Important notes:
//If HPA is set (max < native max) commands to restore or set will be aborted. if (device->drive_info.IdentifyData.ata.Word082 & BIT10) //HPA feature set
//DMA mode commands for identify and set only possible if supported (check identify bit)
//If frozen, identify, identify dma, resotre, set will all be aborted. If id bit shows supported, but the dco ident fails, consider the drive in a frozen state -TJE

bool is_DCO_Supported(tDevice* device, bool* dmaSupport)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT11)
            || (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT11))
        {
            supported = true;
            if (dmaSupport)
            {
                *dmaSupport = false;
                if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word053) && device->drive_info.IdentifyData.ata.Word053 & BIT1) /* this is a validity bit for field 69 */
                    && (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word069) && device->drive_info.IdentifyData.ata.Word069 & BIT12))
                {
                    *dmaSupport = true;
                }
            }
        }
    }
    return supported;
}

eReturnValues dco_Restore(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_DCO_Supported(device, M_NULLPTR))
    {
        ret = ata_DCO_Restore(device);
        if (ret == ABORTED)
        {
            //check if frozen or not using a DCO identify
            dcoData dcoDataForTestingFrozen;
            memset(&dcoDataForTestingFrozen, 0, sizeof(dcoData));
            if (SUCCESS != dco_Identify(device, &dcoDataForTestingFrozen))
            {
                ret = FROZEN;
            }
        }
    }
    return ret;
}

eReturnValues dco_Freeze_Lock(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_DCO_Supported(device, M_NULLPTR))
    {
        ret = ata_DCO_Freeze_Lock(device);
        if (ret == ABORTED)
        {
            ret = FROZEN;//device is already in a frozen state, so return this instead of aborted.
        }
    }
    return ret;
}

eReturnValues dco_Identify(tDevice* device, ptrDcoData data)
{
    eReturnValues ret = NOT_SUPPORTED;
    bool dcoDMASupport = false;
    if (is_DCO_Supported(device, &dcoDMASupport))
    {
        if (data)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, dcoIdentData, 512);
            if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
            {
                dcoDMASupport = false;
            }
            ret = ata_DCO_Identify(device, dcoDMASupport, dcoIdentData, 512);
            if (ret == ABORTED)
            {
                //if the command aborted, then device is in the frozen state so return this instead.-TJE
                ret = FROZEN;
            }
            else
            {
                if (ret == WARN_INVALID_CHECKSUM)
                {
                    ret = SUCCESS;
                    data->validChecksum = false;
                }
                else
                {
                    data->validChecksum = true;
                }
                //setup the local dcoData struct with the word bitfields.
                data->revision = M_BytesTo2ByteValue(dcoIdentData[1], dcoIdentData[0]);
                uint16_t mwdma = M_BytesTo2ByteValue(dcoIdentData[3], dcoIdentData[2]);
                data->mwdma.mwdma0 = mwdma & BIT0;
                data->mwdma.mwdma1 = mwdma & BIT1;
                data->mwdma.mwdma2 = mwdma & BIT2;
                uint16_t udma = M_BytesTo2ByteValue(dcoIdentData[5], dcoIdentData[4]);
                data->udma.udma0 = udma & BIT0;
                data->udma.udma1 = udma & BIT1;
                data->udma.udma2 = udma & BIT2;
                data->udma.udma3 = udma & BIT3;
                data->udma.udma4 = udma & BIT4;
                data->udma.udma5 = udma & BIT5;
                data->udma.udma6 = udma & BIT6;
                data->maxLBA = M_BytesTo8ByteValue(dcoIdentData[13], dcoIdentData[12], dcoIdentData[11], dcoIdentData[10], dcoIdentData[9], dcoIdentData[8], dcoIdentData[7], dcoIdentData[6]);
                uint16_t features1 = M_BytesTo2ByteValue(dcoIdentData[15], dcoIdentData[14]);
                data->feat1.smartFeature = features1 & BIT0;
                data->feat1.smartSelfTest = features1 & BIT1;
                data->feat1.smartErrorLog = features1 & BIT2;
                data->feat1.ATAsecurity = features1 & BIT3;
                data->feat1.powerUpInStandby = features1 & BIT4;
                data->feat1.readWriteDMAQueued = features1 & BIT5;
                data->feat1.automaticAccousticManagement = features1 & BIT6;
                data->feat1.hostProtectedArea = features1 & BIT7;
                data->feat1.fourtyEightBitAddress = features1 & BIT8;
                data->feat1.streaming = features1 & BIT9;
                data->feat1.timeLimitedCommands = features1 & BIT10;
                data->feat1.forceUnitAccess = features1 & BIT11;
                data->feat1.smartSelectiveSelfTest = features1 & BIT12;
                data->feat1.smartConveyanceSelfTest = features1 & BIT13;
                data->feat1.writeReadVerify = features1 & BIT14;
                uint16_t sataFeatures1 = M_BytesTo2ByteValue(dcoIdentData[17], dcoIdentData[16]);
                data->sataFeat.ncqFeature = sataFeatures1 & BIT0;
                data->sataFeat.nonZeroBufferOffsets = sataFeatures1 & BIT1;
                data->sataFeat.interfacePowerManagement = sataFeatures1 & BIT2;
                data->sataFeat.asynchronousNotification = sataFeatures1 & BIT3;
                data->sataFeat.softwareSettingsPreservation = sataFeatures1 & BIT4;
                //sata reserved in word 9
                //words 10-20 reserved
                uint16_t features2 = M_BytesTo2ByteValue(dcoIdentData[43], dcoIdentData[42]);
                data->feat2.extendedPowerConditions = features2 & BIT9;
                data->feat2.dataSetManagement = features2 & BIT10;
                data->feat2.freeFall = features2 & BIT11;
                data->feat2.trustedComputing = features2 & BIT12;
                data->feat2.writeUncorrectable = features2 & BIT13;
                data->feat2.nvCachePowerManagement = features2 & BIT14;
                data->feat2.nvCache = features2 & BIT15;
                //data->features3 = M_BytesTo2ByteValue(dcoIdentData[45], dcoIdentData[44]);
                //words 23-207 reserved
                //words 208-254 reserved
            }
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    return ret;
}

void show_DCO_Identify_Data(ptrDcoData data)
{
    if (data)
    {
        printf("\n===============================\n");
        printf(" DCO Identify Changable Fields \n");
        printf("===============================\n");
        printf("Data Revision: %" PRIu16 "\n", data->revision);
        printf("Multi-word DMA modes (MWDMA):\n");
        if (data->mwdma.mwdma2)
        {
            printf("\tMWDMA 2 (16.7 MB/s)\n");
        }
        if (data->mwdma.mwdma1)
        {
            printf("\tMWDMA 1 (13.3 MB/s)\n");
        }
        if (data->mwdma.mwdma0)
        {
            printf("\tMWDMA 0 (4.2 MB/s)\n");
        }
        printf("Ultra DMA modes (UDMA):\n");
        if (data->udma.udma6)
        {
            printf("\tUDMA 6 (133 MB/s)\n");
        }
        if (data->udma.udma5)
        {
            printf("\tUDMA 5 (100 MB/s)\n");
        }
        if (data->udma.udma4)
        {
            printf("\tUDMA 4 (66.7 MB/s)\n");
        }
        if (data->udma.udma3)
        {
            printf("\tUDMA 3 (44.4 MB/s)\n");
        }
        if (data->udma.udma2)
        {
            printf("\tUDMA 2 (33.3 MB/s)\n");
        }
        if (data->udma.udma1)
        {
            printf("\tUDMA 1 (25 MB/s)\n");
        }
        if (data->udma.udma0)
        {
            printf("\tUDMA 0 (16.7 MB/s)\n");
        }
        printf("Maximum LBA: %" PRIu64 "\n", data->maxLBA);
        printf("Command set/Features 1:\n");
        if (data->feat1.writeReadVerify)
        {
            printf("\tWrite-Read-Verify\n");
        }
        if (data->feat1.smartConveyanceSelfTest)
        {
            printf("\tSMART Conveyance Self-test\n");
        }
        if (data->feat1.smartSelectiveSelfTest)
        {
            printf("\tSMART Selective Self-test\n");
        }
        if (data->feat1.forceUnitAccess)
        {
            printf("\tForced Unit Access\n");
        }
        if (data->feat1.timeLimitedCommands)
        {
            printf("\tTime Limited Commands (TLC)\n");
        }
        if (data->feat1.streaming)
        {
            printf("\tStreaming\n");
        }
        if (data->feat1.fourtyEightBitAddress)
        {
            printf("\t48-bit Addressing\n");
        }
        if (data->feat1.hostProtectedArea)
        {
            printf("\tHost Protected Area (HPA)\n");
        }
        if (data->feat1.automaticAccousticManagement)
        {
            printf("\tAutomatic Acoustic Management (AAM)\n");
        }
        if (data->feat1.readWriteDMAQueued)
        {
            printf("\tRead/Write DMA Queued (TCQ)\n");
        }
        if (data->feat1.powerUpInStandby)
        {
            printf("\tPower Up In Standby (PUIS)\n");
        }
        if (data->feat1.ATAsecurity)
        {
            printf("\tATA Security\n");
        }
        if (data->feat1.smartErrorLog)
        {
            printf("\tSMART Error Logging\n");
        }
        if (data->feat1.smartSelfTest)
        {
            printf("\tSMART Self-test\n");
        }
        if (data->feat1.smartFeature)
        {
            printf("\tSMART Feature\n");
        }
        printf("SATA Command set/Features:\n");
        if (data->sataFeat.softwareSettingsPreservation)
        {
            printf("\tSoftware Settings Preservation (SSP)\n");
        }
        if (data->sataFeat.asynchronousNotification)
        {
            printf("\tAsynchronous Notification\n");
        }
        if (data->sataFeat.interfacePowerManagement)
        {
            printf("\tInterface Power Management\n");
        }
        if (data->sataFeat.nonZeroBufferOffsets)
        {
            printf("\tNon-zero Buffer Offsets\n");
        }
        if (data->sataFeat.ncqFeature)
        {
            printf("\tNative Command Queueing Feature (NCQ)\n");
        }
        printf("Command set/Features 2:\n");
        if (data->feat2.nvCache)
        {
            printf("\tNon-Volatile Cache (NV Cache)\n");
        }
        if (data->feat2.nvCachePowerManagement)
        {
            printf("\tNV Cache Power Management\n");
        }
        if (data->feat2.writeUncorrectable)
        {
            printf("\tWrite Uncorrectable\n");
        }
        if (data->feat2.trustedComputing)
        {
            printf("\tTrusted Computing (TCG)\n");
        }
        if (data->feat2.freeFall)
        {
            printf("\tFree-fall Control\n");
        }
        if (data->feat2.dataSetManagement)
        {
            printf("\tData Set Management (TRIM)\n");
        }
        if (data->feat2.extendedPowerConditions)
        {
            printf("\tExtended Power Conditions (EPC)\n");
        }
        if (!data->validChecksum)
        {
            printf("WARNING: Drive returned invalid checksum on DCO Identify data!\n");
        }
    }
    return;
}

eReturnValues dco_Set(tDevice* device, ptrDcoData data)
{
    eReturnValues ret = NOT_SUPPORTED;
    bool dcoDMASupport = false;
    if (is_DCO_Supported(device, &dcoDMASupport))
    {
        if (data)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, dcoIdentData, 512);
            if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
            {
                dcoDMASupport = false;
            }
            ret = ata_DCO_Identify(device, dcoDMASupport, dcoIdentData, 512);
            if (ret == ABORTED)
            {
                //if the command aborted, then device is in the frozen state so return this instead.-TJE
                ret = FROZEN;
            }
            else
            {
                //go through the user-provided details and make changes to the requested fields
                //mwdma bits
                if (!data->mwdma.mwdma2)
                {
                    M_CLEAR_BIT8(dcoIdentData[2], 2);
                }
                if (!data->mwdma.mwdma1)
                {
                    M_CLEAR_BIT8(dcoIdentData[2], 1);
                }
                if (!data->mwdma.mwdma0)
                {
                    M_CLEAR_BIT8(dcoIdentData[2], 0);
                }
                //udma
                if (!data->udma.udma6)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 6);
                }
                if (!data->udma.udma5)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 5);
                }
                if (!data->udma.udma4)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 4);
                }
                if (!data->udma.udma3)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 3);
                }
                if (!data->udma.udma2)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 2);
                }
                if (!data->udma.udma1)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 1);
                }
                if (!data->udma.udma0)
                {
                    M_CLEAR_BIT8(dcoIdentData[4], 0);
                }
                //maxLBA
                dcoIdentData[13] = M_Byte7(data->maxLBA);
                dcoIdentData[12] = M_Byte6(data->maxLBA);
                dcoIdentData[11] = M_Byte5(data->maxLBA);
                dcoIdentData[10] = M_Byte4(data->maxLBA);
                dcoIdentData[9] = M_Byte3(data->maxLBA);
                dcoIdentData[8] = M_Byte2(data->maxLBA);
                dcoIdentData[7] = M_Byte1(data->maxLBA);
                dcoIdentData[6] = M_Byte0(data->maxLBA);
                //features 1
                if (!data->feat1.writeReadVerify)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 6);
                }
                if (!data->feat1.smartConveyanceSelfTest)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 5);
                }
                if (!data->feat1.smartSelectiveSelfTest)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 4);
                }
                if (!data->feat1.forceUnitAccess)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 3);
                }
                if (!data->feat1.timeLimitedCommands)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 2);
                }
                if (!data->feat1.streaming)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 1);
                }
                if (!data->feat1.fourtyEightBitAddress)
                {
                    M_CLEAR_BIT8(dcoIdentData[15], 0);
                }
                if (!data->feat1.hostProtectedArea)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 7);
                }
                if (!data->feat1.automaticAccousticManagement)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 6);
                }
                if (!data->feat1.readWriteDMAQueued)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 5);
                }
                if (!data->feat1.powerUpInStandby)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 4);
                }
                if (!data->feat1.ATAsecurity)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 3);
                }
                if (!data->feat1.smartErrorLog)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 2);
                }
                if (!data->feat1.smartSelfTest)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 1);
                }
                if (!data->feat1.smartFeature)
                {
                    M_CLEAR_BIT8(dcoIdentData[14], 0);
                }
                //dcoIdentData[16] = M_Byte0(data->sataFeatures1);
                //dcoIdentData[17] = M_Byte1(data->sataFeatures1);
                if (!data->sataFeat.softwareSettingsPreservation)
                {
                    M_CLEAR_BIT8(dcoIdentData[16], 4);
                }
                if (!data->sataFeat.asynchronousNotification)
                {
                    M_CLEAR_BIT8(dcoIdentData[16], 3);
                }
                if (!data->sataFeat.interfacePowerManagement)
                {
                    M_CLEAR_BIT8(dcoIdentData[16], 2);
                }
                if (!data->sataFeat.nonZeroBufferOffsets)
                {
                    M_CLEAR_BIT8(dcoIdentData[16], 1);
                }
                if (!data->sataFeat.ncqFeature)
                {
                    M_CLEAR_BIT8(dcoIdentData[16], 0);
                }
                //sata reserved in word 9
                //words 10-20 reserved
                //feature set 2
                if (!data->feat2.nvCache)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 7);
                }
                if (!data->feat2.nvCachePowerManagement)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 6);
                }
                if (!data->feat2.writeUncorrectable)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 5);
                }
                if (!data->feat2.trustedComputing)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 4);
                }
                if (!data->feat2.freeFall)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 3);
                }
                if (!data->feat2.dataSetManagement)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 2);
                }
                if (!data->feat2.extendedPowerConditions)
                {
                    M_CLEAR_BIT8(dcoIdentData[43], 1);
                }
                //dcoIdentData[44] = M_Byte0(data->features3);
                //dcoIdentData[45] = M_Byte1(data->features3);
                //words 23-207 reserved
                //words 208-254 reserved
                //fields set, setup the checksum
                set_ATA_Checksum_Into_Data_Buffer(dcoIdentData, 512);
                ret = ata_DCO_Set(device, dcoDMASupport, dcoIdentData, 512);
                //TODO: Need to handle if HPA is set since the DCO set will fail
            }
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    return ret;
}
