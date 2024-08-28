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
// \partition_info.h  Read and dump partition table info

#pragma once

#if defined (__cpluspluc)
extern "C"
{
#endif

#include "common_types.h"
#include "operations_Common.h"

    //There are a few other types out there that are not currently supported in here: 
    //https://unix.stackexchange.com/questions/289389/what-are-the-differences-between-the-various-partition-tables
    //https://www.linux.org/threads/partition-tables.9298/
    //
    typedef enum _ePartTableType
    {
        PARTITION_TABLE_NOT_FOUND = 0,
        PARTITION_TABLE_MRB, //master boot record...note there are subtypes...should EBR be here too?
        PARTITION_TABLE_APM, //TODO: apple partition map
        PARTITION_TABLE_GPT  //NOTE: This may include a protective MBR.
    }ePartTableType;

    //since there are some variations, if we can detect them, this enum will help
    typedef enum _eMBRType
    {
        MBR_TYPE_NONE,
        MBR_TYPE_CLASSIC, //4 records
        MBR_TYPE_MODERN,  //4 records
        MBR_TYPE_UEFI,    //1 record most likely, but follows modern/classic other than zeroes in bootstrap code area
        MBR_TYPE_AAP,     //advanced active partitions. AAP is always at 5 if available. Check partition type for this offset
        MBR_TYPE_NEWLDR,  //4 records + newldr and aap. newldr always at 4, aap at 5 if available (check partition type)
        MBR_TYPE_AST_NEC_SPEEDSTOR, //up to 8 records
        MBR_TYPE_ONTRACK_DISK_MANAGER, //up to 16 records
    }eMBRType;

    typedef struct _mbrCHSAddress
    {
        uint8_t head;
        uint8_t sector; //note bits 7:6 are high bits of cylinder field
        uint8_t cylinder;
    }mbrCHSAddress;

    typedef struct _mbrPartitionEntry
    {
        uint8_t status;//bit7 = active. 01h-7Fh are invalid. 0x00 = inactive. AAP and newldr uses some of this differently! If one of these is detected, change how to part this struct
        mbrCHSAddress startingAddress;
        uint8_t partitionType;//https://en.m.wikipedia.org/wiki/Partition_type
        mbrCHSAddress endingAddress;
        uint32_t lbaOfFirstSector;
        uint32_t numberOfSectorsInPartition;
    }mbrPartitionEntry;

#define MBR_CLASSIC_MAX_PARTITIONS (4)
#define MBR_MAX_PARTITIONS (16) //ontrack disk manager allowed up to 16

    //singature is little endian! hi=byte 511, lo=byte510
#define MBR_SIGNATURE_HI (0xAA)
#define MBR_SIGNATURE_LO (0x55)

//https://en.m.wikipedia.org/wiki/Master_boot_record
    typedef struct _mbrData
    {
        eMBRType mbrType;
        uint8_t numberOfPartitions;// set to how many of the following partitions contain something filled in.
        mbrPartitionEntry partition[MBR_MAX_PARTITIONS];
        //MBR type unique fields?
    }mbrData, * ptrMBRData;

    static M_INLINE void safe_free_mbrdata(mbrData **mbr)
    {
        safe_Free(M_REINTERPRET_CAST(void**, mbr));
    }

    //Please read through the lists on both of these websites. This list may be incomplete and be aware that some identifiers are reused between OSs
    //https://en.m.wikipedia.org/wiki/Partition_type
    //https://www.win.tue.nl/~aeb/partitions/partition_types.html
    //some of these that share codes may be able to be figured out with other available partitions or if LBAs are provided for access -TJE
    //Reading the partition format structures may also be able to further clarrify what it is. Ex: & can be NTFS, HPFS, exFAT, so checking this could help
    typedef enum _eMBRPartitionType
    {
        MBR_PART_TYPE_EMPTY                                                 = 0x00,
        MBR_PART_TYPE_DOS_FAT_12                                            = 0x01,
        MBR_PART_TYPE_XENIX_ROOT                                            = 0x02,
        MBR_PART_TYPE_XENIX_USR                                             = 0x03,
        MBR_PART_TYPE_DOS_FAT_16_LT_32MB                                    = 0x04,
        MBR_PART_TYPE_DOS_EXTENDED_PARTITION                                = 0x05,
        MBR_PART_TYPE_DOS_FAT_16_GT_32MB                                    = 0x06,
        MBR_PART_TYPE_OS2_IFS_HPFS                                          = 0x07,
        MBR_PART_TYPE_WINDOWS_NT_NTFS                                       = 0x07,
        MBR_PART_TYPE_EXFAT                                                 = 0x07,
        MBR_PART_TYPE_ADVANCED_UNIX                                         = 0x07,
        MBR_PART_TYPE_QNX2                                                  = 0x07,//pre 1988
        MBR_PART_TYPE_AIX_BOOT                                              = 0x08,
        MBR_PART_TYPE_SPLIT_DRIVE                                           = 0x08,
        MBR_PART_TYPE_COMMODORE_DOS                                         = 0x08,
        MBR_PART_TYPE_DELL_SPANNING_PARTITION                               = 0x08,//spans across multiple drives
        MBR_PART_TYPE_QNX_QNY                                               = 0x08,//QNX 1.x and 2.x
        MBR_PART_TYPE_AIX_DATA                                              = 0x09,
        MBR_PART_TYPE_COHERENT_FS                                           = 0x09,
        MBR_PART_TYPE_QNX_QNZ                                               = 0x09,//1.x and 2.x
        MBR_PART_TYPE_OS2_BOOT_MANAGER                                      = 0x0A,
        MBR_PART_TYPE_COHERENT_SWAP                                         = 0x0A,
        MBR_PART_TYPE_OPUS_0A                                               = 0x0A,
        MBR_PART_TYPE_WIN95_OSR2_FAT32                                      = 0x0B,
        MBR_PART_TYPE_WIN95_OSR2_FAT32_LBA                                  = 0x0C,
        MBR_PART_TYPE_SILICON_SAFE                                          = 0x0D,
        MBR_PART_TYPE_WIN95_DOS_FAT_16_LBA                                  = 0x0E,
        MBR_PART_TYPE_WIN95_EXTENDED_PARTITION_LBA                          = 0x0F,
        MBR_PART_TYPE_OPUS_10                                               = 0x10,
        MBR_PART_TYPE_HIDDEN_DOS_FAT12                                      = 0x11,
        MBR_PART_TYPE_LEADING_EDGE_DOS_16_LOGICALLY_SECTORED                = 0x11,//logically sectored FAT - sector size > 512
        MBR_PART_TYPE_CONFIG_OR_DIAG_PARTITION                              = 0x12,//multiple vendors seems to have used this for a config/recovery/diag partition of some kind
        MBR_PART_TYPE_HIDDEN_DOS_FAT_16_LT_32MB                             = 0x14,
        MBR_PART_TYPE_AST_DOS_FAT_16_LOGICALLY_SECTORED                     = 0x14,
        MBR_PART_TYPE_HIDDEN_DOS_FAT_16_GT_32MB                             = 0x16,
        MBR_PART_TYPE_HIDDEN_IFS_HPFS                                       = 0x17,
        MBR_PART_TYPE_AST_SMARTSLEEP_PARTITION                              = 0x18,
        MBR_PART_TYPE_WILLOWTECH_PHOTON                                     = 0x19,
        MBR_PART_TYPE_HIDDEN_WIN95_OSR2_FAT32                               = 0x1B,
        MBR_PART_TYPE_HIDDEN_WIN95_OSR2_FAT32_LBA                           = 0x1C,
        MBR_PART_TYPE_HIDDEN_WIN95_FAT_16_LBA                               = 0x1E,
        MBR_PART_TYPE_WILLOWTECH_OVERTURE_FS                                = 0x20,//OSF1
        MBR_PART_TYPE_HP_VOL_EXPANSION                                      = 0x21,//speedstor variant?
        MBR_PART_TYPE_FSO2_OXYGEN_FS                                        = 0x22,
        MBR_PART_TYPE_NEC_DOS                                               = 0x24,//logically sectored. Up to 8 partition entries
        MBR_PART_TYPE_RECOVERY_SERVICE_FS                                   = 0x27,//a couple different recovery environments used this
        MBR_PART_TYPE_MIR_OS                                                = 0x27,//BSD variant
        MBR_PART_TYPE_ROUTER_BOOT_KERNEL_PARTITION                          = 0x27,
        MBR_PART_TYPE_ATHE_OS_FS                                            = 0x2A,
        MBR_PART_TYPE_SYLLABLE_SECURE                                       = 0x2B,
        MBR_PART_TYPE_DIGITAL_RESEARCH_PERSONAL_CP_M_86                     = 0x30,
        MBR_PART_TYPE_NOS                                                   = 0x32,
        MBR_PART_TYPE_JFS_ON_OS2_OR_ECS                                     = 0x35,//IBM's JFS from AIX for US/2 or eCS
        MBR_PART_TYPE_THEOS                                                 = 0x38,//v2.3 2GB partition
        MBR_PART_TYPE_PLAN9                                                 = 0x39,
        MBR_PART_TYPE_THEOS_V4_SPANNED                                      = 0x39,
        MBR_PART_TYPE_THEOS_VER4_4GB                                        = 0x3A,
        MBR_PART_TYPE_THEOS_V4_EXTENDED                                     = 0x3B,
        MBR_PART_TYPE_PARTITION_MAGIC_RECOVERY                              = 0x3C,
        MBR_PART_TYPE_HIDDEN_NETWARE                                        = 0x3D,
        MBR_PART_TYPE_VENIX_80286                                           = 0x40,
        MBR_PART_TYPE_PICK                                                  = 0x40,
        MBR_PART_TYPE_LINUX_MINUX                                           = 0x41,//shared with DRDOS
        MBR_PART_TYPE_PERSONAL_RISC_BOOT                                    = 0x41,
        MBR_PART_TYPE_PPC_PREP_BOOT                                         = 0x41,
        MBR_PART_TYPE_LINUX_SWAP                                            = 0x42,
        MBR_PART_TYPE_SFS                                                   = 0x42,//secure file system
        MBR_PART_TYPE_WINDOWS_2000_DYNAMIC_EXTENDED_PARTITION_MARKER        = 0x42,
        MBR_PART_TYPE_LINUX_NATIVE                                          = 0x43,//sharing with DRDOS
        MBR_PART_TYPE_GOBACK                                                = 0x44,
        MBR_PART_TYPE_BOOT_US_BOOT_MANAGER                                  = 0x45,
        MBR_PART_TYPE_PRIAM                                                 = 0x45,
        MBR_PART_TYPE_EUMEL_ELAN_45                                         = 0x45,
        MBR_PART_TYPE_EUMEL_ELAN_46                                         = 0x46,
        MBR_PART_TYPE_EUMEL_ELAN_47                                         = 0x47,
        MBR_PART_TYPE_EUMEL_ELAN_48                                         = 0x48,
        MBR_PART_TYPE_ALFS_THIN_FS_FOR_DOS                                  = 0x4A,
        MBR_PART_TYPE_ADAOS_AQUILA                                          = 0x4A,
        MBR_PART_TYPE_OBERTON_AOS_A2_FS                                     = 0x4C,
        MBR_PART_TYPE_QNX_4                                                 = 0x4D,
        MBR_PART_TYPE_QNX_4_PART2                                           = 0x4E,
        MBR_PART_TYPE_QNX_4_PART3                                           = 0x4F,
        MBR_PART_TYPE_OBERTON_NAT_FS                                        = 0x4F,
        MBR_PART_TYPE_ONTRACK_DISK_MANAGER_RO                               = 0x50,
        MBR_PART_TYPE_LYNX_RTOS                                             = 0x50,
        MBR_PART_TYPE_NATIVE_OBERTON_ALT                                    = 0x50,
        MBR_PART_TYPE_ONTRACK_DISK_MANAGER_RW                               = 0x51,
        MBR_PART_TYPE_NOVELL                                                = 0x51,
        MBR_PART_TYPE_CP_M                                                  = 0x52,
        MBR_PART_TYPE_MICROSOFT_SYS_V_AT                                    = 0x52,
        MBR_PART_TYPE_DISK_MANAGER_6_AUX3                                   = 0x53,
        MBR_PART_TYPE_DISK_MANAGER_6_DUNAMIC_DRIVE_OVERALY                  = 0x54,
        MBR_PART_TYPE_EZ_DRIVE                                              = 0x55,
        MBR_PART_TYPE_GOLDEN_BOX_VFEATURE                                   = 0x56,
        MBR_PART_TYPE_DM_CONVERTED_TO_EZ_BIOS                               = 0x56,
        MBR_PART_TYPE_ATT_MS_DOS_3                                          = 0x56,//logically sectored FAT
        MBR_PART_TYPE_DRIVE_PRO                                             = 0x57,
        MBR_PART_TYPE_VNDI_PARTITION                                        = 0x57,//netware. Unused?
        MBR_PART_TYPE_PRIAM_EDISK                                           = 0x5C,
        MBR_PART_TYPE_SPEED_STOR                                            = 0x61,
        MBR_PART_TYPE_UNIX_SYSTEM_V                                         = 0x63,
        MBR_PART_TYPE_PC_ARMOUR_PROTECTED_PARTITION                         = 0x64,
        MBR_PART_TYPE_NOVELL_NETWARE_286_V2                                 = 0x64,
        MBR_PART_TYPE_NOVELL_NETWARE_386                                    = 0x65, //v3 or v4
        MBR_PART_TYPE_NOVELL_NETWARE_SMS                                    = 0x66,
        MBR_PART_TYPE_NOVELL_WOLF_MOUNTAIN                                  = 0x67,
        MBR_PART_TYPE_NOVELL_68                                             = 0x68,
        MBR_PART_TYPE_NOVELL_NETWARE_5_NSS_PARTITION                        = 0x69,
        MBR_PART_TYPE_DRAGONFLY_BSD                                         = 0x6C,
        MBR_PART_TYPE_DISC_SECURE_MULTIBOOT                                 = 0x70,
        MBR_PART_TYPE_UNIX_V7_X86                                           = 0x72,
        MBR_PART_TYPE_SCRAMDISK_PARTITION                                   = 0x74,
        MBR_PART_TYPE_IBM_PC_IX                                             = 0x75,
        MBR_PART_TYPE_M2FS_M2CS                                             = 0x77,
        MBR_PART_TYPE_VNDI_PART                                             = 0x77,
        MBR_PART_TYPE_XOSL_FS                                               = 0x78,
        MBR_PART_TYPE_PRIMOCACHE_L2                                         = 0x7E,
        MBR_PART_TYPE_ALTERNATIVE_OS_DEVELOPMENT_PARTITION_STATDARD         = 0x7F,
        MBR_PART_TYPE_MINUX_EARLY                                           = 0x80,//up to 1.4a
        MBR_PART_TYPE_MINUX                                                 = 0x81,//Minux 1.4b and later and early linux
        MBR_PART_TYPE_MITAC_DISK_MANAGER                                    = 0x81,
        MBR_PART_TYPE_PRIME                                                 = 0x82,
        MBR_PART_TYPE_SOLARIS_X86                                           = 0x82,
        MBR_PART_TYPE_LINUX_SWAP_82                                         = 0x82,
        MBR_PART_TYPE_LINUX_NATIVE_PARTITION                                = 0x83,//can be different FS types, not just ext<id>
        MBR_PART_TYPE_OS2_HIDDEN_C_DRIVE                                    = 0x84,
        MBR_PART_TYPE_HIBERNATION_PARTITION                                 = 0x84,//Microsoft, Windows 98+
        MBR_PART_TYPE_LINUX_EXT_PARTITION                                   = 0x85,
        MBR_PART_TYPE_OLD_LINUX_RAID_SUPERBLOCK                             = 0x86,
        MBR_PART_TYPE_FAT_16_VOLUME_SET                                     = 0x86,//legacy fault tolerant fat16. NT4 or earlier will add 80h to partitions part of fault tolerant set
        MBR_PART_TYPE_NTFS_VOLUME_SET                                       = 0x87,//Legacy fault-tolerant NTFS.
        MBR_PART_TYPE_LINUX_PLAINTEXT_PARTITION_TABLE                       = 0x88,
        MBR_PART_TYPE_LINUX_KERNEL_PARTITION_AIR_BOOT                       = 0x8A,
        MBR_PART_TYPE_LEGACY_FAULT_TOLERANT_FAT_32                          = 0x8B,
        MBR_PART_TYPE_LEGACY_FAULT_TOLERANT_FAT_32_UUSING_BIOS_EXT_INT13    = 0x8C,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_FAT_12                  = 0x8D,
        MBR_PART_TYPE_LINUX_LOGICAL_VOLUME_MANAGER_PARTITION                = 0x8E,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_FAT_16                  = 0x90,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_DOS_EXTENDED_PARTITION              = 0x91,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_LARGE_FAT_12            = 0x92,
        MBR_PART_TYPE_HIDDEN_LINUX_NATIVE_PARTITION                         = 0x93,
        MBR_PART_TYPE_AMOEBA                                                = 0x93,
        MBR_PART_TYPE_AMOEBA_BAD_BLOCK_TABLE                                = 0x94,
        MBR_PART_TYPE_MIT_EXOPC                                             = 0x95,
        MBR_PART_TYPE_CHRP_ISO_9660                                         = 0x96,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_FAT_32                  = 0x97,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_FAT_32_LBA              = 0x98,
        MBR_PART_TYPE_DATALIGHT_ROM_DOS_SUPER_BOOT                          = 0x98,
        MBR_PART_TYPE_DCE376_LOGICAL_DRIVE                                  = 0x99,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_PRIMARY_DOS_FAT_16_lba              = 0x9A,
        MBR_PART_TYPE_FREE_FDISK_HIDDEN_DOS_EXTENDED_PARTITION_LBA          = 0x9B,
        MBR_PART_TYPE_FORTH_OS                                              = 0x9E,
        MBR_PART_TYPE_BSD_OS                                                = 0x9F,//BSD/OS or BSDI
        MBR_PART_TYPE_LAPTOP_HIBERNATION                                    = 0xA0,
        MBR_PART_TYPE_LAPTOP_HIBERNATION_2                                  = 0xA1,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_A1                                = 0xA1,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_A3                                = 0xA3,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_A4                                = 0xA4,
        MBR_PART_TYPE_386BSD                                                = 0xA5,//Also NetBSD and FreeBSD
        MBR_PART_TYPE_OPENBSD                                               = 0xA6,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_A6                                = 0xA6,
        MBR_PART_TYPE_NEXT_STEP                                             = 0xA7,//NeXTStep
        MBR_PART_TYPE_MAC_OSX                                               = 0xA8,
        MBR_PART_TYPE_NETBSD                                                = 0xA9,//since Feb 19-98
        MBR_PART_TYPE_OLIVETTI_FAT_12_1_44_MB_SERVICE_PARTITION             = 0xAA,//1.44MB service partition
        MBR_PART_TYPE_MAC_OSX_BOOT                                          = 0xAB,
        MBR_PART_TYPE_GO                                                    = 0xAB,//GO! OS
        MBR_PART_TYPE_RISC_OS_ADFS                                          = 0xAD,
        MBR_PART_TYPE_SHAG_OS_FS                                            = 0xAE,
        MBR_PART_TYPE_SHAG_OS_SWAP                                          = 0xAF,
        MBR_PART_TYPE_MAC_OSX_HFS                                           = 0xAF,
        MBR_PART_TYPE_BOOTSTART_DUMMY                                       = 0xB0,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_B1                                = 0xB1,
        MBR_PART_TYPE_QNX_NEUTRINO_POWER_SAFE_FS                            = 0xB1,
        MBR_PART_TYPE_QNX_NEUTRINO_POWER_SAFE_FS_2                          = 0xB2,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_B3                                = 0xB3,
        MBR_PART_TYPE_QNX_NEUTRINO_POWER_SAFE_FS_3                          = 0xB3,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_B4                                = 0xB4,
        MBR_PART_TYPE_HP_VOLUME_EXPANSION_B6                                = 0xB6,
        MBR_PART_TYPE_CORRUPTED_WINDOWS_NT_MIRROR_SET_FAT_16                = 0xB6,
        MBR_PART_TYPE_CORRUPTED_WINDOWS_NT_MIRROR_SET_NTFS                  = 0xB7,
        MBR_PART_TYPE_BSDI_FS                                               = 0xB7,
        MBR_PART_TYPE_BSDI_SWAP                                             = 0xB8,
        MBR_PART_TYPE_BOOT_WIZARD_HIDDEN                                    = 0xBB,
        MBR_PART_TYPE_ACRONIS_BACKUP_PARTITION                              = 0xBC,
        MBR_PART_TYPE_BONNY_DOS_286                                         = 0xBD,
        MBR_PART_TYPE_SOLARIS_8_BOOT                                        = 0xBE,
        MBR_PART_TYPE_NEW_SOLARIS_X86                                       = 0xBF,
        MBR_PART_TYPE_CTOS                                                  = 0xC0,
        MBR_PART_TYPE_REAL_32_SECURE_SMALL_PARTITION                        = 0xC0,
        MBR_PART_TYPE_NTFT                                                  = 0xC0,//netware
        MBR_PART_TYPE_DRDOS_NOVEL_DOS_SECURE                                = 0xC0,
        MBR_PART_TYPE_HIDDEN_LINUX_C2                                       = 0xC2,
        MBR_PART_TYPE_HIDDEN_LINUX_SWAP_C3                                  = 0xC3,
        MBR_PART_TYPE_DRDOS_SECURED_FAT_16_LT_32MB                          = 0xC4,
        MBR_PART_TYPE_DRDOS_SECURED_EXT                                     = 0xC5,
        MBR_PART_TYPE_DRDOS_SECURED_FAT_16_GT_32MB                          = 0xC6,
        MBR_PART_TYPE_WINDOWS_NT_CORRUPED_FAT_16_VOLUME_STRIPE_SET          = 0xC6,
        MBR_PART_TYPE_WINDOWS_NT_CORRUPTED_NTFS_VOLUME_STRIPE_SET           = 0xC7,
        MBR_PART_TYPE_SYRINX_BOOT                                           = 0xC7,
        MBR_PART_TYPE_DRDOS_V7_SECURED_FAT_32_CHS                           = 0xCB,
        MBR_PART_TYPE_DRDOS_V7_SECURED_FAT_32_LBA                           = 0xCC,
        MBR_PART_TYPE_CTOS_MEMDUMP                                          = 0xCD,//?
        MBR_PART_TYPE_DRDOS_V7_FAT_16_X_LBA                                 = 0xCE,
        MBR_PART_TYPE_DRDOS_V7_SECURED_EXT_DOS_LBA                          = 0xCF,
        MBR_PART_TYPE_REAL_32_SECURE_BIG_PARTITION                          = 0xD0,
        MBR_PART_TYPE_MULTIUSER_DOS_SECURED_PARTITION                       = 0xD0,
        MBR_PART_TYPE_OLD_MULTIUSER_DOS_SECURED_FAT_12                      = 0xD1,
        MBR_PART_TYPE_OLD_MULTIUSER_DOS_SECURED_FAT_16_LT_32MB              = 0xD4,
        MBR_PART_TYPE_OLD_MULTIUSER_DOS_SECURED_EXT_PARTITION               = 0xD5,
        MBR_PART_TYPE_OLD_MULTIUSER_DOS_SECURED_FAT_16_GT_32MB              = 0xD6,
        MBR_PART_TYPE_CP_M_86                                               = 0xD8,//CP/M-86
        MBR_PART_TYPE_NON_DS_DATA                                           = 0xDA,
        MBR_PART_TYPE_POWERCOPY_BACKUP                                      = 0xDA,
        MBR_PART_TYPE_DIGITAL_RESEARCH_CP_M                                 = 0xDB,
        MBR_PART_TYPE_CONCURRENT_CP_M                                       = 0xDB,
        MBR_PART_TYPE_CONCURRENT_DOS                                        = 0xDB,
        MBR_PART_TYPE_CTOS_DB                                               = 0xDB,//Convergent Technologies OS - Unisys
        MBR_PART_TYPE_KDG_TELEMETRY_SCPU_BOOT                               = 0xDB,
        MBR_PART_TYPE_HIDDENT_CTOS_MEMDUMP                                  = 0xDD,//?
        MBR_PART_TYPE_DELL_POWEREDGE_SERVER_UTILITIES                       = 0xDE,//FAT fs
        MBR_PART_TYPE_DG_UX_VIRTUAL_DISK_MANAGER                            = 0xDF,
        MBR_PART_TYPE_BOOTIT_EMBRM                                          = 0xDF,
        MBR_PART_TYPE_ST_AVFS                                               = 0xE0,//ST Microelectronics
        MBR_PART_TYPE_DOS_ACCESS_OR_SPEEDSTOR_FAT_12_EXT                    = 0xE1,
        MBR_PART_TYPE_DOS_RO_OR_SPEEDSTOR                                   = 0xE3,
        MBR_PART_TYPE_SPEEDSTOR_FAT_16_EXT_LT_1024_CYL                      = 0xE4,
        MBR_PART_TYPE_TANDY_MSDOS_LOGICALLY_SECTORED_FAT                    = 0xE5,
        MBR_PART_TYPE_STORAGE_DIMENSIONS_SPEEDSTOR_E6                       = 0xE6,
        MBR_PART_TYPE_LUKS                                                  = 0xE8,//Linux unified key setup
        MBR_PART_TYPE_RUFUS_EXTRA                                           = 0xEA,
        MBR_PART_TYPE_FREE_DESKTOP_BOOT                                     = 0xEA,
        MBR_PART_TYPE_BEOS_BFS                                              = 0xEB,
        MBR_PART_TYPE_SKYOS_SKY_FS                                          = 0xEC,
        MBR_PART_TYPE_SPRYTIX                                               = 0xED,
        MBR_PART_TYPE_GPT_PROTECTIVE_PARTITION                              = 0xEE,
        MBR_PART_TYPE_UEFI_SYSTEM_PARTITION                                 = 0xEF,
        MBR_PART_TYPE_LINUX_PA_RISC_BOOT_LOADER                             = 0xF0,
        MBR_PART_TYPE_STORAGE_DIMENSIONS_SPEEDSTOR_F1                       = 0xF1,
        MBR_PART_TYPE_DOS_V3_3_SECONDARY                                    = 0xF2,
        MBR_PART_TYPE_SPEEDSTOR_LARGE                                       = 0xF4,
        MBR_PART_TYPE_PROLOGUE_SINGLE_VOLUME                                = 0xF4,
        MBR_PART_TYPE_PROLOGUE_MULTI_VOLUME                                 = 0xF5,
        MBR_PART_TYPE_STORAGE_DIMENSIONS_SPEEDSTOR_F6                       = 0xF6,
        MBR_PART_TYPE_DDRDRIVE_SOLID_STATE_FS                               = 0xF7,
        MBR_PART_TYPE_PCACHE                                                = 0xF9,//persisten cache...ext2/ext3?
        MBR_PART_TYPE_BOCHS                                                 = 0xFA,
        MBR_PART_TYPE_VMWARE_FS                                             = 0xFB,
        MBR_PART_TYPE_VMWARE_SWAP                                           = 0xFC,
        MBR_PART_TYPE_LINUX_RAID_AUTODETECT_PERSISTENT_SUPERBLOCK           = 0xFD,
        MBR_PART_TYPE_SPEEDSTORE_GT_1024_CYL                                = 0xFE,
        MBR_PART_TYPE_LANSTEP                                               = 0xFE,
        MBR_PART_TYPE_IBM_PS2_IML                                           = 0xFE,//initial microcode load. end of disk
        MBR_PART_TYPE_WINDOWS_NT_DISK_ADMIN_HIDDEN_PARTITION                = 0xFE,
        MBR_PART_TYPE_LINUX_LOGICAL_VOLUME_MANAGER_OLD                      = 0xFE,
        MBR_PART_TYPE_XENIX_BAD_BLOCK_TABLE                                 = 0xFF
    }eMBRPartitionType;

    //----------------------------------------APM--------------------------------------

    //https://en.m.wikipedia.org/wiki/Apple_Partition_Map
    //https://support.apple.com/kb/TA21692?locale=en_US

    //NOTE: Block 0 is a device descriptor map. actual APM may begin at sector 1 or even later

#define APM_SIG_0 'P'
#define APM_SIG_1 'M'

#define APM_MAX_PARTITIONS (62)

#define APM_PARTITION_NAME_LEN (32)
#define APM_PARTITION_TYPE_LEN (32)

#define APM_PROCESSOR_TYPE_LEN (16)

    typedef struct _apmPartitionEntry
    {
        uint32_t startingSector;
        uint32_t partitionSizeSectors;
        char name[APM_PARTITION_NAME_LEN];//right side null padded
        char type[APM_PARTITION_TYPE_LEN];//right side null padded
        uint32_t startingSectorOfDataAreaInPartition;
        uint32_t sizeOfDataAreaInPartitionSectors;
        uint32_t status;
        uint32_t startingSectorOfBootCode;
        uint32_t sizeOfBootCodeBytes;
        uint32_t addressOfBootLoaderCode;
        uint32_t bootCodeEntryPoint;
        uint32_t bootCodeChecksum;
        char processorType[APM_PROCESSOR_TYPE_LEN];//right side null padded
    }apmPartitionEntry;

    typedef struct _apmData
    {
        uint32_t firstPartitionSectorNumber;//more useful for debugging this code than anything else.-TJE Was this found on sector 0, 1, 2, etc...
        uint8_t numberOfPartitions;
        apmPartitionEntry partition[APM_MAX_PARTITIONS];
    }apmData, * ptrAPMData;

    static M_INLINE void safe_free_apmdata(apmData **apm)
    {
        safe_Free(M_REINTERPRET_CAST(void**, apm));
    }

    //----------------------------------------GPT--------------------------------------

#define GPT_HEADER_SIGNATURE_STR "EFI PART"
#define GPT_HEADER_VAL UINT64_C(0x5452415020494645) //note: little endian

#define GPT_PARTITION_ATTR_PLATFORM_REQUIRED BIT0
#define GPT_PARTITION_ATTR_EFI_FW_IGNORE BIT1
#define GPT_PARTITION_ATTR_LEGACY_BIOS_BOOTABLE BIT2 //eq to bit7 in status flag for mbr

#define GPT_GUID_LEN_BYTES (16)
#define GPT_MIN_PARTITIONS (128) //This is the minimum number that is required per UEFI spec for space in the array. There can be more than this.

#define GPT_PARTITION_NAME_LENGTH_BYTES (72)

    typedef enum _eGPTPartitionType
    {
        GPT_PART_TYPE_UNKNOWN = 0,//unknown what the GUID means in the lookup table.
        GPT_PART_TYPE_UNUSED,
        GPT_PART_TYPE_EFI_SYSTEM,
        GPT_PART_TYPE_LEGACY_MBR,
        GPT_PART_TYPE_GRUB_BIOS_BOOT,
        //Windows
        GPT_PART_TYPE_MICROSOFT_RESERVED,
        GPT_PART_TYPE_MICROSOFT_BASIC_DATA,
        GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_METADATA,
        GPT_PART_TYPE_MICROSOFT_LOGICAL_DISK_MANAGER_DATA,
        GPT_PART_TYPE_WINDOWS_RECOVERY_ENVIRONMENT,
        GPT_PART_TYPE_IBM_GPFS,
        GPT_PART_TYPE_STORAGE_SPACES,
        GPT_PART_TYPE_STORAGE_REPLICA,
        //Linux
        GPT_PART_TYPE_LINUX_FS_DATA,
        GPT_PART_TYPE_LINUX_RAID,
        GPT_PART_TYPE_LINUX_ROOT_X86,
        GPT_PART_TYPE_LINUX_ROOT_X86_64,
        GPT_PART_TYPE_LINUX_ROOT_ARM32,
        GPT_PART_TYPE_LINUX_ROOT_AARCH64,
        GPT_PART_TYPE_LINUX_BOOT,
        GPT_PART_TYPE_LINUX_SWAP,
        GPT_PART_TYPE_LINUX_LVM,
        GPT_PART_TYPE_LINUX_HOME,
        GPT_PART_TYPE_LINUX_SRV,
        GPT_PART_TYPE_LINUX_PLAIN_DM_CRYPT,
        GPT_PART_TYPE_LINUX_LUKS,
        GPT_PART_TYPE_LINUX_RESERVED,
        //Mac OSX
        GPT_PART_TYPE_MACOS_HFS_PLUS,
        GPT_PART_TYPE_MACOS_APFS_CONTAINER,
        GPT_PART_TYPE_MACOS_UFS_CONTAINER,
        GPT_PART_TYPE_MACOS_ZFS,
        GPT_PART_TYPE_MACOS_RAID,
        GPT_PART_TYPE_MACOS_RAID_OFFLINE,
        GPT_PART_TYPE_MACOS_BOOT_RECOVERY_HD,
        GPT_PART_TYPE_MACOS_LABEL,
        GPT_PART_TYPE_MACOS_TV_RECOVERY,
        GPT_PART_TYPE_MACOS_CORE_STORAGE_CONTAINER,
        GPT_PART_TYPE_MACOS_APFS_PREBOOT,
        GPT_PART_TYPE_MACOS_APFS_RECOVERY,
        //FreeBSD
        GPT_PART_TYPE_FREEBSD_BOOT,
        GPT_PART_TYPE_FREEBSD_BSD_DISKLABEL,
        GPT_PART_TYPE_FREEBSD_SWAP,
        GPT_PART_TYPE_FREEBSD_UFS,
        GPT_PART_TYPE_FREEBSD_VINUM_VOLUME_MANAGER,
        GPT_PART_TYPE_FREEBSD_ZFS,
        GPT_PART_TYPE_FREEBSD_NANDFS,
        //Solaris/illumos
        GPT_PART_TYPE_SOLARIS_BOOT,
        GPT_PART_TYPE_SOLARIS_ROOT,
        GPT_PART_TYPE_SOLARIS_SWAP,
        GPT_PART_TYPE_SOLARIS_BACKUP,
        GPT_PART_TYPE_SOLARIS_USR,
        GPT_PART_TYPE_SOLARIS_VAR,
        GPT_PART_TYPE_SOLARIS_HOME,
        GPT_PART_TYPE_SOLARIS_ALTERNATE_SECTOR,
        GPT_PART_TYPE_SOLARIS_RESERVED_1,
        GPT_PART_TYPE_SOLARIS_RESERVED_2,
        GPT_PART_TYPE_SOLARIS_RESERVED_3,
        GPT_PART_TYPE_SOLARIS_RESERVED_4,
        GPT_PART_TYPE_SOLARIS_RESERVED_5,
        //NetBSD
        GPT_PART_TYPE_NETBSD_SWAP,
        GPT_PART_TYPE_NETBSD_FFS,
        GPT_PART_TYPE_NETBSD_LFS,
        GPT_PART_TYPE_NETBSD_RAID,
        GPT_PART_TYPE_NETBSD_CONCATENATED,
        GPT_PART_TYPE_NETBSD_ENCRYPTED,
        //OpenBSD
        GPT_PART_TYPE_OPENBSD_DATA,
        //VMWare ESXI
        GPT_PART_TYPE_VMWARE_ESXI_VMKCORE,
        GPT_PART_TYPE_VMWARE_ESXI_VMFS,
        GPT_PART_TYPE_VMWARE_ESXI_RESERVED,
        //Midnight BSD
        GPT_PART_TYPE_MIDNIGHT_BSD_BOOT,
        GPT_PART_TYPE_MIDNIGHT_BSD_DATA,
        GPT_PART_TYPE_MIDNIGHT_BSD_SWAP,
        GPT_PART_TYPE_MIDNIGHT_BSD_UFS,
        GPT_PART_TYPE_MIDNIGHT_BSD_VINUM_VOLUME_MANAGER,
        GPT_PART_TYPE_MIDNIGHT_BSD_ZFS,
        //HP UX
        GPT_PART_TYPE_HP_UX_DATA,
        GPT_PART_TYPE_HP_UX_SERVICE
    }eGPTPartitionType;

    //8-4-4-4-12 in characters
    //NOTE: This struct will be byte swapped as needed to host endianness so it can be easier to print/compare/etc
    typedef struct _gptGUID
    {
        uint32_t part1;//source is le
        uint16_t part2;//source is le
        uint16_t part3;//source is le
        uint16_t part4;//source is be
        uint8_t part5[6];//source is be
    }gptGUID;

    typedef struct _gptPartitionTypeName
    {
        gptGUID guid;
        eGPTPartitionType partition;
        const char* name;
    }gptPartitionTypeName;

    typedef struct _gptPartitionEntry
    {
        gptPartitionTypeName partitionTypeGUID;//NOTE: This will use an internal lookup when populating to set name and enum type. Be aware not all types are known, so it may be set to unknown!
        gptGUID uniquePartitionGUID;
        uint64_t startingLBA;
        uint64_t endingLBA;//inclusive
        uint64_t attributeFlags;//some flags depend on partition type! bits48-63 are type specific
        uint16_t partitionName[GPT_PARTITION_NAME_LENGTH_BYTES / 2];//NOTE: This is described as a null-terminated string. Unclear if ascii or utf-16, but assuming utf-16 for now-TJE
    }gptPartitionEntry;

    typedef struct _gptData
    {
        bool mbrValid;
        mbrData protectiveMBR;
        uint32_t revision;
        bool crc32HeaderValid;//if this is false, then something is wrong and the data may be invalid
        uint64_t currentLBA;//This will be "1" for primary copy. If this was read from the backup, this will be maxlba (or close to it)
        uint64_t backupLBA;
        uint64_t firstUsableLBA;
        uint64_t lastUsableLBA;
        gptGUID diskGUID;
        uint32_t numberOfPartitionEntries;//reported in GPT header. may be greater than number read depending on how many empty entries are in the list
        bool crc32PartitionEntriesValid;
        bool validBackupGPT; //gpt was able to read from last LBA. If reading from the backup, this bool means the primary copy...which will likely be false since the primary was not the data source
        uint32_t partitionDataAvailable;//number of partitions that were successfully read into the following partition entires
        gptPartitionEntry partition[1];//NOTE: This must be allocated based on how many partitions are actually available! ex: malloc(sizeof(gptData) + (get_GPT_Partition_Count() * sizeof(gptPartitionEntry)));
    }gptData, * ptrGPTData;

    static M_INLINE void safe_free_gptdata(gptData **gpt)
    {
        safe_Free(M_REINTERPRET_CAST(void**, gpt));
    }

    //Ideas when reading this info. Note whether the partitions are aligned per the drive's requirements (physical sector size for SAS/SATA, nvme alignment???)

    typedef struct _partitionInfo
    {
        ePartTableType partitionDataType;
        uint32_t diskBlockSize;//In bytes. 512B, 4096B, etc
        union {
            ptrMBRData mbrTable;
            ptrAPMData apmTable;
            ptrGPTData gptTable;
        };
    }partitionInfo, * ptrPartitionInfo;

    static M_INLINE void safe_free_partition_info(partitionInfo **info)
    {
        safe_Free(M_REINTERPRET_CAST(void**, info));
    }

    OPENSEA_OPERATIONS_API ptrPartitionInfo get_Partition_Info(tDevice* device);

    OPENSEA_OPERATIONS_API void print_Partition_Info(ptrPartitionInfo partitionTable);

    OPENSEA_OPERATIONS_API ptrPartitionInfo delete_Partition_Info(ptrPartitionInfo partInfo);

#if defined (__cpluspluc)
}
#endif
