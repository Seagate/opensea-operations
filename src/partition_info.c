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
// \partition_info.c  Read and dump partition table info



//This file has a function to read the partition info from MBR, APM, or GPT partitioning on a storage drive.
//This only reads the information and does not modify it.

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

#include "operations_Common.h"
#include "partition_info.h"

ptrPartitionInfo delete_Partition_Info(ptrPartitionInfo partInfo)
{
    if (partInfo)
    {
        switch (partInfo->partitionDataType)
        {
        case PARTITION_TABLE_NOT_FOUND:
            break;
        case PARTITION_TABLE_MRB:
            safe_free_mbrdata(&partInfo->mbrTable);
            break;
        case PARTITION_TABLE_APM:
            safe_free_apmdata(&partInfo->apmTable);
            break;
        case PARTITION_TABLE_GPT:
            safe_free_gptdata(&partInfo->gptTable);
            break;
        }
        safe_free_partition_info(&partInfo);
    }
    return partInfo;
}

static void fill_OG_MBR_Partitions(uint8_t* mbrDataBuf, uint32_t mbrDataSize, ptrMBRData mbr)
{
    if (mbrDataBuf && mbrDataSize >= UINT32_C(512) && mbr)
    {
        uint32_t partitionTableOffset = UINT32_C(446);//common offset for main 4 MBR partitions
        uint8_t partitionOffset = UINT8_C(0);
        for (; partitionTableOffset < UINT32_C(510) && partitionOffset < MBR_CLASSIC_MAX_PARTITIONS; partitionTableOffset += UINT32_C(16))
        {
            mbr->partition[partitionOffset].status = mbrDataBuf[partitionTableOffset + 0];
            mbr->partition[partitionOffset].startingAddress.head = mbrDataBuf[partitionTableOffset + 1];
            mbr->partition[partitionOffset].startingAddress.sector = mbrDataBuf[partitionTableOffset + 2];
            mbr->partition[partitionOffset].startingAddress.cylinder = mbrDataBuf[partitionTableOffset + 3];
            mbr->partition[partitionOffset].partitionType = mbrDataBuf[partitionTableOffset + 4];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
            mbr->partition[partitionOffset].endingAddress.head = mbrDataBuf[partitionTableOffset + 5];
            mbr->partition[partitionOffset].endingAddress.sector = mbrDataBuf[partitionTableOffset + 6];
            mbr->partition[partitionOffset].endingAddress.cylinder = mbrDataBuf[partitionTableOffset + 7];
            //assuming next two are little endian. 
            mbr->partition[partitionOffset].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 11], mbrDataBuf[partitionTableOffset + 10], mbrDataBuf[partitionTableOffset + 9], mbrDataBuf[partitionTableOffset + 8]);
            mbr->partition[partitionOffset].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 15], mbrDataBuf[partitionTableOffset + 14], mbrDataBuf[partitionTableOffset + 13], mbrDataBuf[partitionTableOffset + 12]);
            if (mbr->partition[partitionOffset].partitionType != 0)
            {
                partitionOffset += 1;
                mbr->numberOfPartitions += 1;
            }
        }
    }
    return;
}

static eReturnValues fill_MBR_Data(uint8_t* mbrDataBuf, uint32_t mbrDataSize, ptrMBRData mbr)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (mbrDataBuf && mbr && mbrDataSize >= 512 && !is_Empty(mbrDataBuf, 512) && mbrDataBuf[510] == MBR_SIGNATURE_LO && mbrDataBuf[511] == MBR_SIGNATURE_HI)
    {
        fill_OG_MBR_Partitions(mbrDataBuf, mbrDataSize, mbr);//fill in the original 4 records first since these are in the same place for any format
        //While all MBR's have 4 partitions, some variants may have more. Check for their signtures first to get the subtype first
        if (mbrDataBuf[2] == 'N' && mbrDataBuf[3] == 'E' && mbrDataBuf[4] == 'W' && mbrDataBuf[5] == 'L' && mbrDataBuf[6] == 'D' && mbrDataBuf[7] == 'R')
        {
            mbr->mbrType = MBR_TYPE_NEWLDR;
            //newldr format. Partition offset 4 in the array will always grab this data
            mbr->partition[4].status = mbrDataBuf[8];
            mbr->partition[4].startingAddress.head = mbrDataBuf[9];
            mbr->partition[4].startingAddress.sector = mbrDataBuf[10];
            mbr->partition[4].startingAddress.cylinder = mbrDataBuf[11];
            mbr->partition[4].partitionType = mbrDataBuf[12];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
            mbr->partition[4].endingAddress.head = mbrDataBuf[13];
            mbr->partition[4].endingAddress.sector = mbrDataBuf[14];
            mbr->partition[4].endingAddress.cylinder = mbrDataBuf[15];
            //assuming next two are little endian. 
            mbr->partition[4].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[19], mbrDataBuf[18], mbrDataBuf[17], mbrDataBuf[16]);
            mbr->partition[4].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[23], mbrDataBuf[22], mbrDataBuf[21], mbrDataBuf[20]);
            mbr->numberOfPartitions += 1;
            //can also contain AAP format too...
            if (mbrDataBuf[428] == 0x78 && mbrDataBuf[429] == 0x56)//AAP signature (listed as optional) 
            {
                //another partition is available in this case with "special semantics"
                //if this exists it is partition 2. May need some way to note this...
                mbr->partition[5].status = mbrDataBuf[430];
                mbr->partition[5].startingAddress.head = mbrDataBuf[431];
                mbr->partition[5].startingAddress.sector = mbrDataBuf[432];
                mbr->partition[5].startingAddress.cylinder = mbrDataBuf[434];
                mbr->partition[5].partitionType = mbrDataBuf[435];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
                mbr->partition[5].endingAddress.head = mbrDataBuf[436];
                mbr->partition[5].endingAddress.sector = mbrDataBuf[437];
                mbr->partition[5].endingAddress.cylinder = mbrDataBuf[438];
                //assuming next two are little endian. 
                mbr->partition[5].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[442], mbrDataBuf[441], mbrDataBuf[440], mbrDataBuf[439]);
                mbr->partition[5].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[446], mbrDataBuf[445], mbrDataBuf[444], mbrDataBuf[443]);
                mbr->numberOfPartitions += 1;
            }
        }
        else if (mbrDataBuf[428] == 0x78 && mbrDataBuf[429] == 0x56 && mbrDataBuf[430] >= 0x80 && mbrDataBuf[430] <= 0xFE)//AAP signature (listed as optional) and physical drive number
        {
            //AAP format only
            mbr->mbrType = MBR_TYPE_AAP;
            mbr->partition[5].status = mbrDataBuf[430];
            mbr->partition[5].startingAddress.head = mbrDataBuf[431];
            mbr->partition[5].startingAddress.sector = mbrDataBuf[432];
            mbr->partition[5].startingAddress.cylinder = mbrDataBuf[434];
            mbr->partition[5].partitionType = mbrDataBuf[435];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
            mbr->partition[5].endingAddress.head = mbrDataBuf[436];
            mbr->partition[5].endingAddress.sector = mbrDataBuf[437];
            mbr->partition[5].endingAddress.cylinder = mbrDataBuf[438];
            //assuming next two are little endian. 
            mbr->partition[5].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[442], mbrDataBuf[441], mbrDataBuf[440], mbrDataBuf[439]);
            mbr->partition[5].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[446], mbrDataBuf[445], mbrDataBuf[444], mbrDataBuf[443]);
            mbr->numberOfPartitions += 1;
        }
        else if (mbrDataBuf[380] == 0x5A && mbrDataBuf[381] == 0xA5)
        {
            //ast/nec MS-DOS and SpeedStor
            //note: these are in reverse order of the "normal" 4 partition order
            mbr->mbrType = MBR_TYPE_AST_NEC_SPEEDSTOR;
            //swap first 4 before filling in any others to fix the order of the partitions
            mbrPartitionEntry temp;
            memcpy(&temp, &mbr->partition[0], sizeof(mbrPartitionEntry));
            memcpy(&mbr->partition[0], &mbr->partition[3], sizeof(mbrPartitionEntry));
            memcpy(&mbr->partition[3], &temp, sizeof(mbrPartitionEntry));
            //1 and 4 swapped, now 2 and 3
            memcpy(&temp, &mbr->partition[1], sizeof(mbrPartitionEntry));
            memcpy(&mbr->partition[1], &mbr->partition[2], sizeof(mbrPartitionEntry));
            memcpy(&mbr->partition[2], &temp, sizeof(mbrPartitionEntry));
            //now fill in the remaining entries
            uint32_t partitionTableOffset = UINT32_C(430);
            uint16_t partitionOffset = mbr->numberOfPartitions;
            for (; partitionTableOffset < UINT32_C(512) && partitionOffset < MBR_MAX_PARTITIONS && partitionTableOffset >= 380; partitionTableOffset -= UINT32_C(16))
            {
                mbr->partition[partitionOffset].status = mbrDataBuf[partitionTableOffset + 0];
                mbr->partition[partitionOffset].startingAddress.head = mbrDataBuf[partitionTableOffset + 1];
                mbr->partition[partitionOffset].startingAddress.sector = mbrDataBuf[partitionTableOffset + 2];
                mbr->partition[partitionOffset].startingAddress.cylinder = mbrDataBuf[partitionTableOffset + 3];
                mbr->partition[partitionOffset].partitionType = mbrDataBuf[partitionTableOffset + 4];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
                mbr->partition[partitionOffset].endingAddress.head = mbrDataBuf[partitionTableOffset + 5];
                mbr->partition[partitionOffset].endingAddress.sector = mbrDataBuf[partitionTableOffset + 6];
                mbr->partition[partitionOffset].endingAddress.cylinder = mbrDataBuf[partitionTableOffset + 7];
                //assuming next two are little endian. 
                mbr->partition[partitionOffset].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 11], mbrDataBuf[partitionTableOffset + 10], mbrDataBuf[partitionTableOffset + 9], mbrDataBuf[partitionTableOffset + 8]);
                mbr->partition[partitionOffset].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 15], mbrDataBuf[partitionTableOffset + 14], mbrDataBuf[partitionTableOffset + 13], mbrDataBuf[partitionTableOffset + 12]);
                if (mbr->partition[partitionOffset].partitionType != 0)
                {
                    partitionOffset += 1;
                    mbr->numberOfPartitions += 1;
                }
            }

        }
        else if (mbrDataBuf[252] == 0xAA && mbrDataBuf[253] == 0x55)
        {
            //ontrack disk manager MBR
            mbr->mbrType = MBR_TYPE_ONTRACK_DISK_MANAGER;
            //assume remaining start at 254 and increment up to 430
            uint32_t partitionTableOffset = UINT32_C(254);
            uint8_t partitionOffset = mbr->numberOfPartitions;
            for (; partitionTableOffset < UINT32_C(430) && partitionOffset < MBR_MAX_PARTITIONS; partitionTableOffset += UINT32_C(16))
            {
                mbr->partition[partitionOffset].status = mbrDataBuf[partitionTableOffset + 0];
                mbr->partition[partitionOffset].startingAddress.head = mbrDataBuf[partitionTableOffset + 1];
                mbr->partition[partitionOffset].startingAddress.sector = mbrDataBuf[partitionTableOffset + 2];
                mbr->partition[partitionOffset].startingAddress.cylinder = mbrDataBuf[partitionTableOffset + 3];
                mbr->partition[partitionOffset].partitionType = mbrDataBuf[partitionTableOffset + 4];//if zero, this is empty! This seems to be the most common way to figure this out anyways-TJE
                mbr->partition[partitionOffset].endingAddress.head = mbrDataBuf[partitionTableOffset + 5];
                mbr->partition[partitionOffset].endingAddress.sector = mbrDataBuf[partitionTableOffset + 6];
                mbr->partition[partitionOffset].endingAddress.cylinder = mbrDataBuf[partitionTableOffset + 7];
                //assuming next two are little endian. 
                mbr->partition[partitionOffset].lbaOfFirstSector = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 11], mbrDataBuf[partitionTableOffset + 10], mbrDataBuf[partitionTableOffset + 9], mbrDataBuf[partitionTableOffset + 8]);
                mbr->partition[partitionOffset].numberOfSectorsInPartition = M_BytesTo4ByteValue(mbrDataBuf[partitionTableOffset + 15], mbrDataBuf[partitionTableOffset + 14], mbrDataBuf[partitionTableOffset + 13], mbrDataBuf[partitionTableOffset + 12]);
                if (mbr->partition[partitionOffset].partitionType != 0)
                {
                    partitionOffset += 1;
                    mbr->numberOfPartitions += 1;
                }
            }
        }
        else if (is_Empty(mbrDataBuf, 424) && mbrDataBuf[444] == 0 && mbrDataBuf[445] == 0)//UEFI systems will have zeros in the boot code
        {
            mbr->mbrType = MBR_TYPE_UEFI;
        }
        //most common is modern MBR. No real signature, but can look for areas with specific zeroes
        else if (mbrDataBuf[218] == 0 && mbrDataBuf[219] == 0 && ((mbrDataBuf[444] == 0 && mbrDataBuf[445] == 0) || (mbrDataBuf[444] == 0x5A && mbrDataBuf[445] == 0x5A)) && mbrDataBuf[220] >= 0x80)
        {
            //most likely a modern MBR
            mbr->mbrType = MBR_TYPE_MODERN;
        }
        else
        {
            //original or unknown MBR format. Assume 4 partitions only (since this is common in all formats)
            mbr->mbrType = MBR_TYPE_CLASSIC;
        }
        ret = SUCCESS;
    }
    return ret;
}

//This requires pointing data to lba 0 (to keep consistent with other functions)
static uint32_t number_Of_GPT_Partitions(uint8_t* gptDataBuf, uint32_t gptDataSize, uint32_t deviceLogicalBlockSize, uint64_t lba)
{
    uint32_t count = 0;
    if (gptDataBuf && gptDataSize >= (2 * deviceLogicalBlockSize))
    {
        uint32_t gptOffset = deviceLogicalBlockSize;//offset to LBA 1
        if (lba != 0)
        {
            //assuming backup at the end of the device, so need to change the offset!
            gptOffset = gptDataSize - deviceLogicalBlockSize;
        }
        //TODO: validate header is present???
        count = M_BytesTo4ByteValue(gptDataBuf[gptOffset + 83], gptDataBuf[gptOffset + 82], gptDataBuf[gptOffset + 81], gptDataBuf[gptOffset + 80]);
    }
    return count;
}

static uint32_t reverse_bits_32(uint32_t value)
{
    uint32_t reversedValue = UINT32_C(0);
    for (uint8_t bitIndex = UINT8_C(0); bitIndex < UINT8_C(32); ++bitIndex)
    {
        if ((value & (UINT32_C(1) << bitIndex)) != UINT32_C(0))
        {
            reversedValue |= (UINT32_C(1) << (UINT32_C(31) - bitIndex));
        }
    }
    return reversedValue;
}

#define UEFI_CRC32_POLYNOMIAL UINT32_C(0x04C11DB7)

static uint32_t gpt_CRC_32(uint8_t* dataBuf, uint32_t dataLength)
{
    uint32_t crc32 = UINT32_MAX;
    if (dataBuf && dataLength > 0)
    {
        static bool crcTableInitialized = false;
        static uint32_t crcTable[UINT8_MAX + 1] = { UINT32_C(0) };

        if (!crcTableInitialized)
        {
            //need to fill values into the crc table before using it
            for (uint32_t tableIndex = UINT32_C(0); tableIndex < (UINT8_MAX + 1); ++tableIndex)
            {
                uint32_t value = reverse_bits_32(tableIndex);
                for (uint8_t bitIndex = UINT8_C(0); bitIndex < UINT8_C(8); ++bitIndex)
                {
                    if ((value & BIT31) != UINT32_C(0))
                    {
                        value = (value << UINT32_C(1)) ^ UEFI_CRC32_POLYNOMIAL;
                    }
                    else
                    {
                        value <<= UINT32_C(1);
                    }
                }
                crcTable[tableIndex] = reverse_bits_32(value);
            }
            crcTableInitialized = true;
        }

        for (uint32_t iter = UINT32_C(0); iter < dataLength; ++iter)
        {
            uint8_t tableIndex = (crc32 ^ dataBuf[iter]) & UINT8_MAX;
            crc32 = (crc32 >> UINT32_C(8)) ^ crcTable[tableIndex];
        }

        crc32 ^= UINT32_MAX;
    }
    return crc32;
}

//This will work, but the crappy thing is they need to be in ORDER for best performance.
//Rather than ordering this ourselves, put these in a list that is manageable and trackable ourselves, then sort it before using it.-TJE
//https://en.m.wikipedia.org/wiki/GUID_Partition_Table#Partition_type_GUIDs
bool gptGUIDsSorted = false;
gptPartitionTypeName gptGUIDNameLookup[] = {
    //not specific to an OS or software
    {{0x00000000, 0x0000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, GPT_PART_TYPE_UNUSED, "Unused"},
    {{0xC12A7328, 0xF81F, 0x11D2, 0xBA4B, {0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}}, GPT_PART_TYPE_EFI_SYSTEM, "EFI System"},
    {{0x024DEE41, 0x33E7, 0x11DE, 0x9D69, {0x00, 0x08, 0xC7, 0x81, 0xF3, 0x9F}}, GPT_PART_TYPE_LEGACY_MBR, "Legacy MBR"},
    {{0x21686148, 0x6449, 0x6E6F, 0x744E, {0x65, 0x65, 0x64, 0x45, 0x46, 0x49}}, GPT_PART_TYPE_GRUB_BIOS_BOOT, "GRUB BIOS Boot"},
    //Windows
    {{0xE3C9E316, 0x0B5C, 0x4DB8, 0x817D, {0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}}, GPT_PART_TYPE_MICROSOFT_RESERVED, "Microsoft Reserved"},
    {{0xEBD0A0A2, 0xB9E5, 0x4433, 0x87C0, {0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}}, GPT_PART_TYPE_MICROSOFT_BASIC_DATA, "Microsoft Basic Data"},
    {{0x5808C8AA, 0x7E8F, 0x42E0, 0x85D2, {0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}}, GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_METADATA, "Microsoft Logical Disk Manager Metadata"},
    {{0xAF9B60A0, 0x1431, 0x4F62, 0xBC68, {0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD}}, GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_DATA, "Microsoft Logical Disk Manager Data"},
    {{0xDE94BBA4, 0x06D1, 0x4D40, 0xA16A, {0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC}}, GPT_PART_TYPE_WINDOWS_RECOVERY_ENVIRONMENT, "Windows Recovery Environment"},
    {{0x37AFFC90, 0xEF7D, 0x4E96, 0x91C3, {0x2D, 0x7A, 0xE0, 0x55, 0xB1, 0x74}}, GPT_PART_TYPE_IBM_GPFS, "IBM GPFS"},
    {{0xE75CAF8F, 0xF680, 0x4CEE, 0xAFA3, {0xB0, 0x01, 0xE5, 0x6E, 0xFC, 0x2D}}, GPT_PART_TYPE_STORAGE_SPACES, "Storage Spaces"},
    {{0x558D43C5, 0xA1AC, 0x43C0, 0xAAC8, {0xD1, 0x47, 0x2B, 0x29, 0x23, 0xD1}}, GPT_PART_TYPE_STORAGE_REPLICA, "Storage Replica"},
    //Linux
    {{0x0FC63DAF, 0x8483, 0x4772, 0x8E79, {0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}}, GPT_PART_TYPE_LINUX_FS_DATA, "Linux Filesystem Data"},
    {{0xA19D880F, 0x05FC, 0x4D3B, 0xA006, {0x74, 0x3F, 0x0F, 0x84, 0x91, 0x1E}}, GPT_PART_TYPE_LINUX_RAID, "Linux RAID Partition"},
    {{0x44479540, 0xF297, 0x41B2, 0x9AF7, {0xD1, 0x31, 0xD5, 0xF0, 0x45, 0x8A}}, GPT_PART_TYPE_LINUX_ROOT_X86, "Linux Root (x86)"},
    {{0x4F68BCE3, 0xE8CD, 0x4DB1, 0x96E7, {0xFB, 0xCA, 0xF9, 0x84, 0xB7, 0x09}}, GPT_PART_TYPE_LINUX_ROOT_X86_64, "Linux Root (x86_64)"},
    {{0x69DAD710, 0x2CE4, 0x4E3C, 0xB16C, {0x21, 0xA1, 0xD4, 0x9A, 0xBE, 0xD3}}, GPT_PART_TYPE_LINUX_ROOT_ARM32, "Linux Root (32bit ARM)"},
    {{0xB921B045, 0x1DF0, 0x41C3, 0xAF44, {0x4C, 0x6F, 0x28, 0x0D, 0x3F, 0xAE}}, GPT_PART_TYPE_LINUX_ROOT_AARCH64, "Linux Root (64bit ARM/AArch64)"},
    {{0xBC13C2FF, 0x59E6, 0x4262, 0xA352, {0xB2, 0x75, 0xFD, 0x6F, 0x71, 0x72}}, GPT_PART_TYPE_LINUX_BOOT, "Linux /boot"},
    {{0x0657FD6D, 0xA4AB, 0x43C4, 0x84E5, {0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}}, GPT_PART_TYPE_LINUX_SWAP, "Linux Swap"},
    {{0xE6D6D379, 0xF507, 0x44C2, 0xA23C, {0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28}}, GPT_PART_TYPE_LINUX_LVM, "Linux Logical Volume Manager"},
    {{0x933AC7E1, 0x2EB4, 0x4F13, 0xB844, {0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15}}, GPT_PART_TYPE_LINUX_HOME, "Linux /home"},
    {{0x3B8F8425, 0x20E0, 0x4F3B, 0x907F, {0xA1, 0x25, 0xA7, 0x6F, 0x98, 0xE8}}, GPT_PART_TYPE_LINUX_SRV, "Linux /srv"},
    {{0x7FFEC5C9, 0x2D00, 0x49B7, 0x8941, {0x3E, 0xA1, 0x0A, 0x55, 0x86, 0xB7}}, GPT_PART_TYPE_LINUX_PLAIN_DM_CRYPT, "Linux Plain dm-crypt"},
    {{0xCA7D7CCB, 0x63ED, 0x4C53, 0x861C, {0x17, 0x42, 0x53, 0x60, 0x59, 0xCC}}, GPT_PART_TYPE_LINUX_LUKS, "Linux LUKS"},
    {{0x8DA63339, 0x0007, 0x60C0, 0xC436, {0x08, 0x3A, 0xC8, 0x23, 0x09, 0x08}}, GPT_PART_TYPE_LINUX_RESERVED, "Linux Reserved"},
    //Mac OSX
    {{0x48465300, 0x0000, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_HFS_PLUS, "Mac OS HFS+"},
    {{0x7C3457EF, 0x0000, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_APFS_CONTAINER, "Mac OS APFS Container"},
    {{0x55465300, 0x0000, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_UFS_CONTAINER, "Mac OS UFS Container"},
    {{0x6A898CC3, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_MACOS_ZFS, "Mac OS ZFS"},
    {{0x52414944, 0x0000, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_RAID, "Mac OS RAID"},
    {{0x52414944, 0x5F4F, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_RAID_OFFLINE, "Mac OS RAID (offline)"},
    {{0x426F6F74, 0x0000, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_BOOT_RECOVERY_HD, "Mac OS Boot (Recovery HD)"},
    {{0x4C616265, 0x6C00, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_LABEL, "Mac OS Label"},
    {{0x5265636F, 0x7665, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_TV_RECOVERY, "Mac OS TV Recovery"},
    {{0x53746F72, 0x6167, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_CORE_STORAGE_CONTAINER, "Mac OS Core Storage Container"},
    {{0x69646961, 0x6700, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_APFS_PREBOOT, "Mac OS APFS Preboot"},
    {{0x52637672, 0x7900, 0x11AA, 0xAA11, {0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC}}, GPT_PART_TYPE_MACOS_APFS_RECOVERY, "Mac OS APFS Recovery"},
    //FreeBSD
    {{0x83BD6B9D, 0x7F41, 0x11DC, 0xBE0B, {0x00, 0x15, 0x60, 0xB8, 0x4F, 0x0F}}, GPT_PART_TYPE_FREEBSD_BOOT, "FreeBSD Boot"},
    {{0x516E7CB4, 0x6ECF, 0x11D6, 0x8FF8, {0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B}}, GPT_PART_TYPE_FREEBSD_BSD_DISKLABEL, "FreeBSD BSD Disklabel"},
    {{0x516E7CB5, 0x6ECF, 0x11D6, 0x8FF8, {0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B}}, GPT_PART_TYPE_FREEBSD_SWAP, "FreeBSD Swap"},
    {{0x516E7CB6, 0x6ECF, 0x11D6, 0x8FF8, {0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B}}, GPT_PART_TYPE_FREEBSD_UFS, "FreeBSD UFS"},
    {{0x516E7CB8, 0x6ECF, 0x11D6, 0x8FF8, {0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B}}, GPT_PART_TYPE_FREEBSD_VINUM_VOLUME_MANAGER, "FreeBSD Vinum Volume Manage"},
    {{0x516E7CBA, 0x6ECF, 0x11D6, 0x8FF8, {0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B}}, GPT_PART_TYPE_FREEBSD_ZFS, "FreeBSD ZFS"},
    {{0x74BA7DD9, 0xA689, 0x11E1, 0xBD04, {0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}}, GPT_PART_TYPE_FREEBSD_NANDFS, "FreeBSD nandfs"},
    //Solaris/illumos
    {{0x6A82CB45, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_BOOT, "Solaris/Illumos Boot"},
    {{0x6A85CF4D, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_ROOT, "Solaris/Illumos Root"},
    {{0x6A87C46F, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_SWAP, "Solaris/Illumos Swap"},
    {{0x6A8B642B, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_BACKUP, "Solaris/Illumos Backup"},
    {{0x6A898CC3, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_USR, "Solaris/Illumos /usr"},
    {{0x6A8EF2E9, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_VAR, "Solaris/Illumos /var"},
    {{0x6A90BA39, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_HOME, "Solaris/Illumos /home"},
    {{0x6A9283A5, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_ALTERNATE_SECTOR, "Solaris/Illumos Alternate sector"},
    {{0x6A945A3B, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_RESERVED_1, "Solaris/Illumos Reserved 1"},
    {{0x6A9630D1, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_RESERVED_2, "Solaris/Illumos Reserved 2"},
    {{0x6A980767, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_RESERVED_3, "Solaris/Illumos Reserved 3"},
    {{0x6A96237F, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_RESERVED_4, "Solaris/Illumos Reserved 4"},
    {{0x6A8D2AC7, 0x1DD2, 0x11B2, 0x99A6, {0x08, 0x00, 0x20, 0x73, 0x66, 0x31}}, GPT_PART_TYPE_SOLARIS_RESERVED_5, "Solaris/Illumos Reserved 5"},
    //NetBSD
    {{0x49F48D32, 0xB10E, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_SWAP, "NetBSD swap"},
    {{0x49F48D5A, 0xB10E, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_FFS, "NetBSD FFS"},
    {{0x49F48D82, 0xB10E, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_LFS, "NetBSD LFS"},
    {{0x49F48DAA, 0xB10E, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_RAID, "NetBSD RAID"},
    {{0x2DB519C4, 0xB10F, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_CONCATENATED, "NetBSD Concatenated"},
    {{0x2DB519EC, 0xB10F, 0x11DC, 0xB99B, {0x00, 0x19, 0xD1, 0x87, 0x96, 0x48}}, GPT_PART_TYPE_NETBSD_ENCRYPTED, "NetBSD Encrypted"},
    //OpenBSD
    {{0x824CC7A0, 0x36A8, 0x11E3, 0x890A, {0x95, 0x25, 0x19, 0xAD, 0x3F, 0x61}}, GPT_PART_TYPE_OPENBSD_DATA, "OpenBSD data"},
    //VMWare ESXI
    {{0x9D275380, 0x40AD, 0x11DB, 0xBF97, {0x00, 0x0C, 0x29, 0x11, 0xD1, 0xB8}}, GPT_PART_TYPE_VMWARE_ESXI_VMKCORE, "VMWare ESXi vmkcore"},
    {{0xAA31E02A, 0x400F, 0x11DB, 0x9590, {0x00, 0x0C, 0x29, 0x11, 0xD1, 0xB8}}, GPT_PART_TYPE_VMWARE_ESXI_VMFS, "VMWare ESXi VMFS"},
    {{0x9198EFFC, 0x31C0, 0x11DB, 0x8F78, {0x00, 0x0C, 0x29, 0x11, 0xD1, 0xB8}}, GPT_PART_TYPE_VMWARE_ESXI_RESERVED, "VMWare ESXi Reserved"},
    //Midnight BSD
    {{0x85D5E45E, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_BOOT, "Midnight BSD boot"},
    {{0x85D5E45A, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_DATA, "Midnight BSD data"},
    {{0x85D5E45B, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_SWAP, "Midnight BSD swap"},
    {{0x0394EF8B, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_UFS, "Midnight BSD UFS"},
    {{0x85D5E45C, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_VINUM_VOLUME_MANAGER, "Midnight BSD Vinum volume manager"},
    {{0x85D5E45D, 0x237C, 0x11E1, 0xB4B3, {0xE8, 0x9A, 0x8F, 0x7F, 0xC3, 0xA7}}, GPT_PART_TYPE_MIDNIGHT_BSD_ZFS, "Midnight BSD ZFS"},
    //HP UX
    {{0x75894C1E, 0x3AEB, 0x11D3, 0xB7C1, {0x7B, 0x03, 0xA0, 0x00, 0x00, 0x00}}, GPT_PART_TYPE_HP_UX_DATA, "HP UX Data"},
    {{0xE2A1E728, 0x32E3, 0x11D6, 0xA682, {0x7B, 0x03, 0xA0, 0x00, 0x00, 0x00}}, GPT_PART_TYPE_HP_UX_SERVICE, "HP UX Service"}
};

//used with bsearch to locate the name quicker
static int cmp_GPT_Part_GUID(const void* a, const void* b)
{
    return memcmp(&(C_CAST(const gptPartitionTypeName*, a))->guid, &(C_CAST(const gptPartitionTypeName *, b))->guid, sizeof(gptGUID));
}

//This copies the mixed endianness GUID from the dataBuf into a format that can easily be output with a for-loop into the GUID variable
static void copy_GPT_GUID(uint8_t* dataBuf, gptGUID *guid)
{
    if (dataBuf && guid)
    {
        //GUIDs are 8-4-4-4-12 format, mixed endian. First 3 are le, last 2 are be
        //8-4-4-4 need to be byteswapped
        //last 12 can be memcpy'd
        //first 8 characters (4 bytes) -le
        guid->part1 = M_BytesTo4ByteValue(dataBuf[3], dataBuf[2], dataBuf[1], dataBuf[0]);
        //next 4 chars (2 bytes) -le
        guid->part2 = M_BytesTo2ByteValue(dataBuf[5], dataBuf[4]);
        //next 4 chars (2 bytes) -le
        guid->part3 = M_BytesTo2ByteValue(dataBuf[7], dataBuf[6]);
        //next 4 chars (2 bytes) -be
        guid->part4 = M_BytesTo2ByteValue(dataBuf[8], dataBuf[9]);
        //last 12 chars (6 bytes) -be
        guid->part5[0] = dataBuf[10];
        guid->part5[1] = dataBuf[11];
        guid->part5[2] = dataBuf[12];
        guid->part5[3] = dataBuf[13];
        guid->part5[4] = dataBuf[14];
        guid->part5[5] = dataBuf[15];
    }
    return;
}

#define GPT_SIGNATURE_STR_LEN 9

static eReturnValues fill_GPT_Data(tDevice *device, uint8_t* gptDataBuf, uint32_t gptDataSize, ptrGPTData gpt, uint32_t sizeOfGPTDataStruct, uint64_t lba)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (gptDataBuf && gpt && gptDataSize >= UINT32_C(32768) && gptDataSize >= (2 * device->drive_info.deviceBlockSize))
    {
        //In order to "easily" adapt this code for GPT backup, it will move the buffer data around since at the beginning the header is before the partitions, but for the backup
        //the partitions come before the header.
        uint32_t gptHeaderOffset = device->drive_info.deviceBlockSize;
        if (lba != 0)
        {
            gptHeaderOffset = gptDataSize - device->drive_info.deviceBlockSize;
        }

        DECLARE_ZERO_INIT_ARRAY(char, gptSignature, GPT_SIGNATURE_STR_LEN);
        memcpy(gptSignature, &gptDataBuf[gptHeaderOffset], 8);
        gptSignature[GPT_SIGNATURE_STR_LEN - 1] = '\0';
        if (strcmp(gptSignature, "EFI PART") == 0)
        {
            ret = SUCCESS;
            //fill in the MBR first
            if (lba == 0 && SUCCESS == fill_MBR_Data(gptDataBuf, 512, &gpt->protectiveMBR))
            {
                gpt->mbrValid = true;
            }
            gpt->revision = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 11], gptDataBuf[gptHeaderOffset + 10], gptDataBuf[gptHeaderOffset + 9], gptDataBuf[gptHeaderOffset + 8]);
            uint32_t gptHeaderSize = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 15], gptDataBuf[gptHeaderOffset + 14], gptDataBuf[gptHeaderOffset + 13], gptDataBuf[gptHeaderOffset + 12]);
            //save current header value.
            //to calculate the header value, we need the header size (at least 92) and to zero out the crc32 in the data before recalculating it
            uint32_t crc32HeaderValue = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 19], gptDataBuf[gptHeaderOffset + 18], gptDataBuf[gptHeaderOffset + 17], gptDataBuf[gptHeaderOffset + 16]);
            gpt->currentLBA = M_BytesTo8ByteValue(gptDataBuf[gptHeaderOffset + 31], gptDataBuf[gptHeaderOffset + 30], gptDataBuf[gptHeaderOffset + 29], gptDataBuf[gptHeaderOffset + 28], gptDataBuf[gptHeaderOffset + 27], gptDataBuf[gptHeaderOffset + 26], gptDataBuf[gptHeaderOffset + 25], gptDataBuf[gptHeaderOffset + 24]);
            gpt->backupLBA = M_BytesTo8ByteValue(gptDataBuf[gptHeaderOffset + 39], gptDataBuf[gptHeaderOffset + 38], gptDataBuf[gptHeaderOffset + 37], gptDataBuf[gptHeaderOffset + 36], gptDataBuf[gptHeaderOffset + 35], gptDataBuf[gptHeaderOffset + 34], gptDataBuf[gptHeaderOffset + 33], gptDataBuf[gptHeaderOffset + 32]);
            gpt->firstUsableLBA = M_BytesTo8ByteValue(gptDataBuf[gptHeaderOffset + 47], gptDataBuf[gptHeaderOffset + 46], gptDataBuf[gptHeaderOffset + 45], gptDataBuf[gptHeaderOffset + 44], gptDataBuf[gptHeaderOffset + 43], gptDataBuf[gptHeaderOffset + 42], gptDataBuf[gptHeaderOffset + 41], gptDataBuf[gptHeaderOffset + 40]);
            gpt->lastUsableLBA = M_BytesTo8ByteValue(gptDataBuf[gptHeaderOffset + 55], gptDataBuf[gptHeaderOffset + 54], gptDataBuf[gptHeaderOffset + 53], gptDataBuf[gptHeaderOffset + 52], gptDataBuf[gptHeaderOffset + 51], gptDataBuf[gptHeaderOffset + 50], gptDataBuf[gptHeaderOffset + 49], gptDataBuf[gptHeaderOffset + 48]);
            copy_GPT_GUID(&gptDataBuf[gptHeaderOffset + 56], &gpt->diskGUID);
            gpt->numberOfPartitionEntries = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 83], gptDataBuf[gptHeaderOffset + 82], gptDataBuf[gptHeaderOffset + 81], gptDataBuf[gptHeaderOffset + 80]);
            //before going any further, validate the CRC
            //first zero out the CRC as specified in UEFI spec
            memset(&gptDataBuf[gptHeaderOffset + 16], 0, 4);
            //now calculate the CRC for the length (gpt header size)
            if (crc32HeaderValue == gpt_CRC_32(&gptDataBuf[gptHeaderOffset], gptHeaderSize))
            {
                bool usedLocalPartitionBuf = false;
                uint8_t* gptPartitionArray = &gptDataBuf[gptHeaderOffset + device->drive_info.deviceBlockSize];//offset to the beginning of the partition array
                uint32_t gptPartitionArrayDataLength = gptDataSize - (2 * device->drive_info.deviceBlockSize);
                uint32_t sizeOfPartitionEntry = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 87], gptDataBuf[gptHeaderOffset + 86], gptDataBuf[gptHeaderOffset + 85], gptDataBuf[gptHeaderOffset + 84]);
                if (lba != 0)
                {
                    gptPartitionArray = &gptDataBuf[gptDataSize - device->drive_info.deviceBlockSize - (gpt->numberOfPartitionEntries * sizeOfPartitionEntry)];//for backup this will point to the beginning of the partition array
                }
                gpt->crc32HeaderValid = true;
                uint32_t gptStructPartitionEntriesAvailable = C_CAST(uint32_t, (sizeOfGPTDataStruct - (sizeof(gptData) - sizeof(gptPartitionEntry))) / sizeof(gptPartitionEntry));
                //the header passed, so time to validate the CRC of the partition data!
                //need to know how many partition structs we have available to read into before beginning to read
                //need to make sure we have all the databuffer necessary to read all partitions...it is possible that there are more than is in the passed in data pointer
                if (gptPartitionArrayDataLength < (sizeOfPartitionEntry * gpt->numberOfPartitionEntries))
                {
                    uint64_t partitionArrayLBA = UINT64_C(2);//point to beginning of the partition data. This is assuming reading at primary GPT!
                    //reread partition entries
                    usedLocalPartitionBuf = true;
                    //allocate a new buffer to read this in and read only the partition array
                    //calculate the data length and round up to the nearest full logical block
                    gptPartitionArrayDataLength = C_CAST(uint32_t, (((sizeOfPartitionEntry * gpt->numberOfPartitionEntries) + (device->drive_info.deviceBlockSize - UINT64_C(1))) / device->drive_info.deviceBlockSize) * device->drive_info.deviceBlockSize);
                    if (lba != 0)
                    {
                        //calculate the LBA to read the beginning of the partition array!
                        partitionArrayLBA = device->drive_info.deviceMaxLba - (gptPartitionArrayDataLength / device->drive_info.deviceBlockSize);
                    }
                    gptPartitionArray = C_CAST(uint8_t*, safe_calloc(gptPartitionArrayDataLength, sizeof(uint8_t)));
                    if (!gptPartitionArray)
                    {
                        return MEMORY_FAILURE;
                    }
                    ret = read_LBA(device, partitionArrayLBA, false, gptPartitionArray, gptPartitionArrayDataLength);
                    if (ret != SUCCESS)
                    {
                        safe_free(&gptPartitionArray);
                        return FAILURE;
                    }
                }
                uint32_t partitionArrayCRC32 = M_BytesTo4ByteValue(gptDataBuf[gptHeaderOffset + 91], gptDataBuf[gptHeaderOffset + 90], gptDataBuf[gptHeaderOffset + 89], gptDataBuf[gptHeaderOffset + 88]);
                //at this point we should have all information we need to start by running a CRC for validation
                if (partitionArrayCRC32 == gpt_CRC_32(gptPartitionArray, (sizeOfPartitionEntry * gpt->numberOfPartitionEntries)))
                {
                    //valid and we can read the partitions
                    gpt->crc32PartitionEntriesValid = true;
                    for (uint64_t partIter = 0, dataOffset = 0; partIter < gpt->numberOfPartitionEntries && partIter < gptStructPartitionEntriesAvailable; ++partIter, dataOffset += sizeOfPartitionEntry)
                    {
                        gptPartitionTypeName* gptName = M_NULLPTR;
                        copy_GPT_GUID(&gptPartitionArray[dataOffset + 0], &gpt->partition[partIter].partitionTypeGUID.guid);
                        if (!gptGUIDsSorted)
                        {
                            qsort(gptGUIDNameLookup, sizeof(gptGUIDNameLookup) / sizeof(gptGUIDNameLookup[0]), sizeof(gptGUIDNameLookup[0]), cmp_GPT_Part_GUID);
                            gptGUIDsSorted = true;
                        }

                        gptName = C_CAST(gptPartitionTypeName*, bsearch(
                            &gpt->partition[partIter].partitionTypeGUID, gptGUIDNameLookup,
                            sizeof(gptGUIDNameLookup) / sizeof(gptGUIDNameLookup[0]), sizeof(gptGUIDNameLookup[0]),
                            (int(*)(const void*, const void*))cmp_GPT_Part_GUID));

                        if (gptName)
                        {
                            //found a match, so set the partition info in the structure to the matched data
                            memcpy(&gpt->partition[partIter].partitionTypeGUID, gptName, sizeof(gptPartitionTypeName));
                        }

                        copy_GPT_GUID(&gptPartitionArray[dataOffset + GPT_GUID_LEN_BYTES], &gpt->partition[partIter].uniquePartitionGUID);
                        gpt->partition[partIter].startingLBA = M_BytesTo8ByteValue(gptPartitionArray[dataOffset + 39], gptPartitionArray[dataOffset + 38], gptPartitionArray[dataOffset + 37], gptPartitionArray[dataOffset + 36], gptPartitionArray[dataOffset + 35], gptPartitionArray[dataOffset + 34], gptPartitionArray[dataOffset + 33], gptPartitionArray[dataOffset + 32]);
                        gpt->partition[partIter].endingLBA = M_BytesTo8ByteValue(gptPartitionArray[dataOffset + 47], gptPartitionArray[dataOffset + 46], gptPartitionArray[dataOffset + 45], gptPartitionArray[dataOffset + 44], gptPartitionArray[dataOffset + 43], gptPartitionArray[dataOffset + 42], gptPartitionArray[dataOffset + 41], gptPartitionArray[dataOffset + 40]);
                        gpt->partition[partIter].attributeFlags = M_BytesTo8ByteValue(gptPartitionArray[dataOffset + 55], gptPartitionArray[dataOffset + 54], gptPartitionArray[dataOffset + 53], gptPartitionArray[dataOffset + 52], gptPartitionArray[dataOffset + 51], gptPartitionArray[dataOffset + 50], gptPartitionArray[dataOffset + 49], gptPartitionArray[dataOffset + 48]);
                        memcpy(&gpt->partition[partIter].partitionName[0], &gptPartitionArray[dataOffset + 56], GPT_PARTITION_NAME_LENGTH_BYTES);
                        if (!is_Empty(&gpt->partition[partIter].partitionTypeGUID, GPT_GUID_LEN_BYTES))//this should be ok, but may want a different way of doing this
                        {
                            //only increment this count when the GUID is non-zero. A zero for the GUID is an unused GUID
                            ++gpt->partitionDataAvailable;
                        }
                    }
                    //Now that the entire array has been read and all CRCs have been validated, we need to compare to the backup location.
                    //NOTE: If the code populated from the backup LBA already, this verifies the primary LBA...which is expected to be empty otherwise the backup would not have been used.
                }
                if (usedLocalPartitionBuf)
                {
                    safe_free(&gptPartitionArray);
                }
            }
        }
    }
    return ret;
}

ptrPartitionInfo get_Partition_Info(tDevice* device)
{
    ptrPartitionInfo partitionData = C_CAST(ptrPartitionInfo, safe_calloc(1, sizeof(partitionInfo)));
    //This function will read LBA 0 for 32KiB first, enough to handle most situations
    //It will check for MBR, APM, and GPT (not necessarily in that order), then fill in proper structures.
    //If everything is zeros, it will read the last 32KiB of the drive to see if a backup of the boot sector is available.
    uint32_t dataSize = UINT32_C(32768);
    uint8_t* dataBuffer = C_CAST(uint8_t*, safe_calloc(dataSize, sizeof(uint8_t)));
    if (dataBuffer && partitionData)
    {
        uint64_t lba = 0;
        partitionData->diskBlockSize = device->drive_info.deviceBlockSize;
        do
        {
            if (SUCCESS == read_LBA(device, lba, false, dataBuffer, dataSize))//using fua to make sure we are reading from the media, not cache
            {
                DECLARE_ZERO_INIT_ARRAY(char, gptSignature, 9);
                if (lba == 0)
                {
                    memcpy(gptSignature, &dataBuffer[device->drive_info.deviceBlockSize], 8);
                }
                else
                {
                    //check for backup GPT
                    memcpy(gptSignature, &dataBuffer[dataSize - device->drive_info.deviceBlockSize], 8);
                }
                //First check for an MBR. This is most common to find, then check if APM is in sector 1. If neither are found, check if a GPT table exists even without a protective MBR
                //First check the signature bytes
                //NOTE: all documentation I have does not show any backups of the MBR. If a backup is available, this needs to change to support it.-TJE
                if (lba == 0 && dataBuffer[510] == MBR_SIGNATURE_LO && dataBuffer[511] == MBR_SIGNATURE_HI)
                {
                    //A MBR is detected!
                    //check if there is possibly a GPT partition or not
                    partitionData->partitionDataType = PARTITION_TABLE_MRB;
                    //Easy way is to check for EFI part signature
                    if (strcmp(gptSignature, "EFI PART") == 0)
                    {
                        partitionData->partitionDataType = PARTITION_TABLE_GPT;
                    }
                    else
                    {
                        //call functino to fill MBR data
                        partitionData->mbrTable = C_CAST(ptrMBRData, safe_calloc(1, sizeof(mbrData)));
                        if (partitionData->mbrTable)
                        {
                            fill_MBR_Data(dataBuffer, dataSize, partitionData->mbrTable);
                        }
                    }
                }
                else if (lba == 0 && dataBuffer[device->drive_info.deviceBlockSize + 0] == APM_SIG_0 && dataBuffer[device->drive_info.deviceBlockSize + 1] == APM_SIG_1)
                {
                    //APM detected!
                    partitionData->partitionDataType = PARTITION_TABLE_APM;
                    //call function to fill APM data
                }
                if (partitionData->partitionDataType == PARTITION_TABLE_NOT_FOUND && strcmp(gptSignature, "EFI PART") == 0)
                {
                    //GPT table detected!
                    partitionData->partitionDataType = PARTITION_TABLE_GPT;
                }
                if (lba == 0 && partitionData->partitionDataType == PARTITION_TABLE_NOT_FOUND)
                {
                    memset(dataBuffer, 0, dataSize);//clear out any old data in case something weird happens
                    //change the LBA to read from to maxLBA - 32KiB
                    lba = device->drive_info.deviceMaxLba - (dataSize / device->drive_info.deviceBlockSize) + 1;//1 corrects the LBA offset to be able to find the backup GPT partition -TJE
                }
                else if (partitionData->partitionDataType == PARTITION_TABLE_GPT)
                {
                    uint32_t partitionCount = number_Of_GPT_Partitions(dataBuffer, dataSize, device->drive_info.deviceBlockSize, lba);
                    uint32_t gptStructSize = C_CAST(uint32_t, (sizeof(gptData) - sizeof(gptPartitionEntry)) + (sizeof(gptPartitionEntry) * partitionCount));
                    partitionData->gptTable = C_CAST(ptrGPTData, safe_calloc(gptStructSize, sizeof(uint8_t)));
                    if (partitionData->gptTable)
                    {
                        partitionData->partitionDataType = PARTITION_TABLE_GPT;
                        fill_GPT_Data(device, dataBuffer, dataSize, partitionData->gptTable, gptStructSize, lba);
                    }
                    else
                    {
                        partitionData->partitionDataType = PARTITION_TABLE_NOT_FOUND;
                    }
                }
                else
                {
                    //no more places to look for the partition table, so time to exit
                    break;
                }
            }
            else
            {
                printf("Unable to read 32KiB starting at LBA %" PRIu64 "\n", lba);
                break;
            }
        } while (partitionData->partitionDataType == PARTITION_TABLE_NOT_FOUND);
    }
    else
    {
        printf("Error allocating 32KiB to read fro mthe disk.\n");
    }
    return partitionData;
}

static void print_MBR_CHS(mbrCHSAddress address)
{
    uint16_t cylinder = M_BytesTo2ByteValue(M_GETBITRANGE(address.sector, 7, 6), address.cylinder);
    uint8_t sector = M_GETBITRANGE(address.sector, 5, 0);
    printf("%" PRIu16 ":%" PRIu8 ":%" PRIu8, cylinder, address.head, sector);
}

static void print_MBR_Info(ptrMBRData mbrTable)
{
    if (mbrTable)
    {
        printf("---MBR info---\n");
        //bool checkForAAP = false;
        //bool checkForNEWWLDR = false;
        switch (mbrTable->mbrType)
        {
        case MBR_TYPE_NONE:
            printf("Error: This is not a valid MBR\n");
            return;
        case MBR_TYPE_CLASSIC:
            printf("Detected a classic MBR\n");
            break;
        case MBR_TYPE_MODERN:
            printf("Detected a modern MBR\n");
            break;
        case MBR_TYPE_UEFI:    //1 record most likely, but follows modern/classic other than zeroes in bootstrap code area
            printf("Detected a UEFI MBR\n");
            break;
        case MBR_TYPE_AAP:     //advanced active partitions. AAP is always at 5 if available. Check partition type for this offset
            printf("Detected a AAP MBR\n");
            //checkForAAP = true;
            break;
        case MBR_TYPE_NEWLDR:  //4 records + newldr and aap. newldr always at 4, aap at 5 if available (check partition type)
            printf("Detected a NEWLDR MBR\n");
            //checkForAAP = true;
            //checkForNEWWLDR = true;
            break;
        case MBR_TYPE_AST_NEC_SPEEDSTOR: //up to 8 records
            printf("Detected a AST/NEC or Speedstor MBR\n");
            break;
        case MBR_TYPE_ONTRACK_DISK_MANAGER: //up to 16 records
            printf("Detected an ontrack disk manager MBR\n");
            break;
        }
        for (uint8_t partIter = 0; partIter < mbrTable->numberOfPartitions && partIter < MBR_MAX_PARTITIONS; ++partIter)
        {
            if (mbrTable->partition[partIter].partitionType != 0)//make sure this is not empty
            {
                printf("\n\t---Partition %" PRIu8 "---\n", partIter);
                printf("\tPartition Type: ");
                //Print out the type that it is. There are a lot, some reused between systems so this may not always be possible.
                //      Currently focussing on what is most likely to be seen: Windows, Linux, UEFI, MacOSX, one of the BSDs still active today
                //https://en.m.wikipedia.org/wiki/Partition_type
                //https://www.win.tue.nl/~aeb/partitions/partition_types.html
                switch (mbrTable->partition[partIter].partitionType)
                {
                case MBR_PART_TYPE_GPT_PROTECTIVE_PARTITION:
                    printf("GPT Protective\n");
                    break;
                case MBR_PART_TYPE_UEFI_SYSTEM_PARTITION:
                    printf("UEFI System Partition\n");
                    break;
                default:
                    printf("%" PRIX8 "h\n", mbrTable->partition[partIter].partitionType);
                    break;
                }
                //check if bootable by looking for 80h in status
                if (mbrTable->partition[partIter].status == 0x80)
                {
                    printf("\tBootable: True\n");
                }
                else if (mbrTable->partition[partIter].status == 0x00)
                {
                    printf("\tBootable: False\n");
                }
                else
                {
                    //unknwon status flag? Maybe a less common MBR usage for one of the variants
                    printf("\tUnknown Status: %" PRIX8 "h\n", mbrTable->partition[partIter].status);
                }
                //Use function to print starting/ending CHS
                printf("\tStarting CHS: ");
                print_MBR_CHS(mbrTable->partition[partIter].startingAddress);
                printf("\n\tEnding CHS: ");
                print_MBR_CHS(mbrTable->partition[partIter].endingAddress);
                printf("\n");
                printf("\tFirst LBA: %" PRIu32 "\n", mbrTable->partition[partIter].lbaOfFirstSector);
                printf("\tNumber of sectors: %" PRIu32 "\n", mbrTable->partition[partIter].numberOfSectorsInPartition);
                //TODO: Using LBA or CHS, calculate size of partition to print it out in B, KB, GB, etc
            }
        }
    }
    return;
}

static void print_GPT_GUID(gptGUID guid)
{
    printf("%08" PRIX32 "-%04" PRIX16 "-%04" PRIX16 "-%04" PRIX16 "-%02" PRIX8 "%02" PRIX8 "%02" PRIX8 "%02" PRIX8 "%02" PRIX8 "%02" PRIX8,
        guid.part1, guid.part2, guid.part3, guid.part4, guid.part5[0], guid.part5[1], guid.part5[2], guid.part5[3], guid.part5[4], guid.part5[5]);
    return;
}

static void print_GPT_Info(ptrGPTData gptTable)
{
    if (gptTable)
    {
        if (gptTable->mbrValid)
        {
            print_MBR_Info(&gptTable->protectiveMBR);
            printf("\n");
        }
        printf("---GPT info---\n");
        //revision
        printf("\tRevision: %" PRIu16 ".%" PRIu16 "\n", M_Word1(gptTable->revision), M_Word0(gptTable->revision));
        //CRC valid/not valid
        printf("\tHeader CRC32 valid: ");
        if (gptTable->crc32HeaderValid)
        {
            printf("True\n");
        }
        else
        {
            printf("False. WARNING: Following data may be inaccurate!\n");
        }
        printf("\tPartition CRC32 valid: ");
        if (gptTable->crc32PartitionEntriesValid)
        {
            printf("True\n");
        }
        else
        {
            printf("False. WARNING: Following data may be inaccurate!\n");
        }
        //current LBA read and location of backup
        printf("\tGPT LBA: %" PRIu64 "\n", gptTable->currentLBA);
        printf("\tGPT Backup LBA: %" PRIu64 "\n", gptTable->backupLBA);
        //first usable LBA
        printf("\tFirst Usable LBA: %" PRIu64 "\n", gptTable->firstUsableLBA);
        //last usable LBA
        printf("\tLast Usable LBA: %" PRIu64 "\n", gptTable->lastUsableLBA);
        //TODO: Calculate total usable disk size from usable LBAs
        //disk guid
        printf("\tDisk GUID: ");
        print_GPT_GUID(gptTable->diskGUID);
        printf("\n");
        //TODO: mark if backup matched or not
        for (uint64_t partIter = 0; partIter < UINT32_MAX && partIter < gptTable->partitionDataAvailable; ++partIter)
        {
            printf("\n\t---GPT Partition %" PRIu64 "---\n", partIter);
            printf("\tPartition Type GUID: ");
            print_GPT_GUID(gptTable->partition[partIter].partitionTypeGUID.guid);
            printf("\n\tPartition Type: ");
            if (gptTable->partition[partIter].partitionTypeGUID.name)
            {
                printf("%s\n", gptTable->partition[partIter].partitionTypeGUID.name);
            }
            else
            {
                printf("Unknown\n");
            }
            printf("\tUnique Partition GUID: ");
            print_GPT_GUID(gptTable->partition[partIter].uniquePartitionGUID);
            printf("\n\tStarting LBA: %" PRIu64 "\n", gptTable->partition[partIter].startingLBA);
            printf("\tEnding LBA: %" PRIu64 "\n", gptTable->partition[partIter].endingLBA);
            //TODO: Calculate partition size in bytes from starting/ending LBAs
            printf("\tAttributes: %016" PRIX64 "\n", gptTable->partition[partIter].attributeFlags);
            //Output attributes from UEFI spec
            if (gptTable->partition[partIter].attributeFlags & GPT_PARTITION_ATTR_PLATFORM_REQUIRED)
            {
                printf("\t\tPlatform Required Partition\n");
            }
            if (gptTable->partition[partIter].attributeFlags & GPT_PARTITION_ATTR_EFI_FW_IGNORE)
            {
                printf("\t\tEFI Ignore\n");
            }
            if (gptTable->partition[partIter].attributeFlags & GPT_PARTITION_ATTR_LEGACY_BIOS_BOOTABLE)
            {
                printf("\t\tLegacy BIOS Bootable\n");
            }
            //FS specific attributes. Must be matched to a known GUID
            switch (gptTable->partition[partIter].partitionTypeGUID.partition)
            {
            case GPT_PART_TYPE_MICROSOFT_RESERVED:
            case GPT_PART_TYPE_MICROSOFT_BASIC_DATA:
            case GPT_PART_TYPE_WINDOWS_RECOVERY_ENVIRONMENT:
            case GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_METADATA:
            case GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_DATA:
                //https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2012-R2-and-2012/cc753455(v=ws.11)?redirectedfrom=MSDN#Anchor_1
                //https://learn.microsoft.com/en-us/windows/win32/api/vds/ns-vds-create_partition_parameters?redirectedfrom=MSDN
                //NOTE: it is unclear which other partitions may set this in Windows
                if (gptTable->partition[partIter].attributeFlags & BIT63)
                {
                    printf("\t\tDo not assign drive letter\n");
                }
                if (gptTable->partition[partIter].attributeFlags & BIT62)
                {
                    printf("\t\tHide volume from mount manager\n");
                }
                if (gptTable->partition[partIter].attributeFlags & BIT61)
                {
                    printf("\t\tShadow copy\n");
                }
                if (gptTable->partition[partIter].attributeFlags & BIT60)
                {
                    printf("\t\tRead-only\n");
                }
                break;
            case GPT_PART_TYPE_EFI_SYSTEM:
                if (gptTable->partition[partIter].attributeFlags & BIT63)
                {
                    //this may be a microsoft unique special case to not assign a drive letter.
                    //UEFI spec does not mention this at all, but in Windows the EFI system partition will not be assigned a volume letter unless you use diskpart CLI to assign and mount it -TJE
                    printf("\t\tMSFT - Do not assign drive letter\n");
                }
                break;
            default:
                break;
            }
            //TODO: Convert the partition name string into something that can be printed. This is UTF-16, so there are possible cross-platform challenges here -TJE
        }
    }
    return;
}

static void print_APM_Info(ptrAPMData apmTable)
{
    if (apmTable)
    {
        printf("---APM info---\n");

    }
    return;
}

void print_Partition_Info(ptrPartitionInfo partitionTable)
{
    if (partitionTable)
    {
        printf("\n=====================\n");
        printf("   Partition Table   \n");
        printf("=====================\n\n");
        switch (partitionTable->partitionDataType)
        {
        case PARTITION_TABLE_NOT_FOUND:
            printf("No partition table was found.\n");
            printf("NOTE: When validating an erased drive, this is not enough information to say\n");
            printf("      that all user data is successfully erased and unretrievable.\n");
            printf("      Verifcation of erasure must check more of the drive as data recovery software\n");
            printf("      may still be able to recover files when a partition table was deleted, but the\n");
            printf("      rest of the drive was not completely erased.\n\n");
            break;
        case PARTITION_TABLE_MRB:
            print_MBR_Info(partitionTable->mbrTable);
            break;
        case PARTITION_TABLE_APM:
            print_APM_Info(partitionTable->apmTable);
            break;
        case PARTITION_TABLE_GPT:
            print_GPT_Info(partitionTable->gptTable);
            break;
        }
    }
    return;
}
