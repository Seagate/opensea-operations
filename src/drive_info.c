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
#include "drive_info.h"
#include "operations.h" //this is for read-look ahead and write cache information
#include "logs.h"
#include "set_max_lba.h"
#include "smart.h"
#include "scsi_helper.h"
#include "nvme_helper_func.h"
#include "firmware_download.h"
#include "usb_hacks.h"

int get_ATA_Drive_Information(tDevice *device, ptrDriveInformationSAS_SATA driveInfo)
{
    int ret = SUCCESS;
    bool sctSupported = false;
    bool gplSupported = false;
    bool smartErrorLoggingSupported = false;
    bool smartStatusFromSCTStatusLog = false;
    memset(driveInfo, 0, sizeof(driveInformation));
    if (SUCCESS == ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata, LEGACY_DRIVE_SEC_SIZE))
    {
        uint8_t *bytePtr = (uint8_t*)&device->drive_info.IdentifyData.ata.Word000;
        uint16_t *wordPtr = &device->drive_info.IdentifyData.ata.Word000;
        //MN
        memcpy(driveInfo->modelNumber, device->drive_info.IdentifyData.ata.ModelNum, MODEL_NUM_LEN);
        byte_Swap_String(driveInfo->modelNumber);
        remove_Leading_And_Trailing_Whitespace(driveInfo->modelNumber);
        //SN
        memcpy(driveInfo->serialNumber, device->drive_info.IdentifyData.ata.SerNum, SERIAL_NUM_LEN);
        byte_Swap_String(driveInfo->serialNumber);
        remove_Leading_And_Trailing_Whitespace(driveInfo->serialNumber);
        //FWRev
        memcpy(driveInfo->firmwareRevision, device->drive_info.IdentifyData.ata.FirmVer, 8);
        byte_Swap_String(driveInfo->firmwareRevision);
        remove_Leading_And_Trailing_Whitespace(driveInfo->firmwareRevision);
        //WWN
        if (device->drive_info.IdentifyData.ata.Word084 & BIT8 || device->drive_info.IdentifyData.ata.Word087 & BIT8)
        {
            driveInfo->worldWideNameSupported = true;
            memcpy(&driveInfo->worldWideName, &device->drive_info.IdentifyData.ata.Word108, 8); //copy the 8 bytes into the world wide name
            word_Swap_64(&driveInfo->worldWideName); //byte swap to make useful
        }
        //MaxLBA
        if (wordPtr[83] & BIT10)
        {
            driveInfo->maxLBA = M_BytesTo8ByteValue(bytePtr[200], bytePtr[201], bytePtr[202], bytePtr[203], bytePtr[204], bytePtr[205], bytePtr[206], bytePtr[207]);
            byte_Swap_64(&driveInfo->maxLBA);
        }
        else
        {
            uint32_t tempMaxLba = M_BytesTo4ByteValue(bytePtr[120], bytePtr[121], bytePtr[122], bytePtr[123]);
            byte_Swap_32(&tempMaxLba);
            driveInfo->maxLBA = tempMaxLba;
        }
        if (driveInfo->maxLBA > 0)
        {
            driveInfo->maxLBA -= 1;
        }
        //Check if CHS words are non-zero to set if the information is valid.
        if (!(wordPtr[1] == 0 || wordPtr[3] == 0 || wordPtr[6] == 0))
        {
            driveInfo->ataLegacyCHSInfo.legacyCHSValid = true;
            driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders = wordPtr[1];
            driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads = (uint8_t)wordPtr[3];
            driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack = (uint8_t)wordPtr[6];
            if ((wordPtr[53] & BIT0) || (wordPtr[54] != 0 && wordPtr[55] != 0 && wordPtr[56] != 0 && wordPtr[57] != 0 && wordPtr[58] != 0))
            {
                driveInfo->ataLegacyCHSInfo.currentInfoconfigurationValid = true;
                driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders = wordPtr[54];
                driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads = (uint8_t)wordPtr[55];
                driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack = (uint8_t)wordPtr[56];
                driveInfo->ataLegacyCHSInfo.currentCapacityInSectors = M_BytesTo4ByteValue(bytePtr[117], bytePtr[116], bytePtr[115], bytePtr[114]);
            }
        }
        //get the sector sizes from the identify data
        if (((wordPtr[106] & BIT14) == BIT14) && ((wordPtr[106] & BIT15) == 0)) //making sure this word has valid data
        {
            //word 117 is only valid when word 106 bit 12 is set
            if ((wordPtr[106] & BIT12) == BIT12)
            {
                driveInfo->logicalSectorSize = M_BytesTo2ByteValue(wordPtr[118], wordPtr[117]);
                driveInfo->logicalSectorSize *= 2; //convert to words to bytes
            }
            else //means that logical sector size is 512bytes
            {
                driveInfo->logicalSectorSize = 512;
            }
            if ((wordPtr[106] & BIT13) == 0)
            {
                driveInfo->physicalSectorSize = driveInfo->logicalSectorSize;
            }
            else //multiple logical sectors per physical sector
            {
                uint8_t sectorSizeExponent = 0;
                //get the number of logical blocks per physical blocks
                sectorSizeExponent = wordPtr[106] & 0x000F;
                driveInfo->physicalSectorSize = (uint32_t)(driveInfo->logicalSectorSize * power_Of_Two(sectorSizeExponent));
            }
        }
        else
        {
            driveInfo->logicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
            driveInfo->physicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
        }
        //sector alignment
        if (wordPtr[209] & BIT14)
        {
            //bits 13:0 are valid for alignment. bit 15 will be 0 and bit 14 will be 1. remove bit 14 with an xor
            driveInfo->sectorAlignment = (uint16_t)(wordPtr[209] ^ BIT14);
        }
        //rotation rate
        memcpy(&driveInfo->rotationRate, &wordPtr[217], 2);
        //form factor
        driveInfo->formFactor = M_Nibble0(wordPtr[168]);
        //zoned capabilities (ACS4)
        driveInfo->zonedDevice = (uint8_t)(wordPtr[69] & (BIT0 | BIT1));
        //get which specifications are supported and the number of them added to the list (ATA Spec listed in word 80)
        uint16_t specsBits = wordPtr[80];
        //Guessed name as this doesn't exist yet
        if (specsBits & BIT15)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-8");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT14)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-7");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT13)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-6");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT12)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-5");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT11)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-4");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT10)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-3");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT9)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ACS-2");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT8)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA8-ACS");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT7)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA/ATAPI-7");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT6)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA/ATAPI-6");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT5)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA/ATAPI-5");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT4)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA/ATAPI-4");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT3)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA-3");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT2)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA-2");
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT1)
        {
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA-1");
            driveInfo->numberOfSpecificationsSupported++;
        }
        //now get the Transport specs supported.
        specsBits = wordPtr[222];
        uint8_t transportType = M_Nibble3(specsBits);//0 = parallel, 1 = serial, e = PCIe
        if (specsBits & BIT7)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 3.2");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT6)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 3.1");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT5)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 3.0");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT4)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 2.6");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT3)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 2.5");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT2)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA II: Extensions");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT1)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SATA 1.0a");
            }
            else if (transportType == 0)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA/ATAPI-7");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        if (specsBits & BIT0)
        {
            if (transportType == 0x01)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA8-AST");
            }
            else if (transportType == 0)
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "ATA8-APT");
            }
            else
            {
                sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Reserved");
            }
            driveInfo->numberOfSpecificationsSupported++;
        }
        //get if it's FDE/TCG
        if (wordPtr[69] & BIT4)
        {
            //FDE
            driveInfo->encryptionSupport = ENCRYPTION_FULL_DISK;
        }
        if (is_Seagate_Family(device) == SEAGATE && wordPtr[243] & BIT14)
        {
            //FDE
            driveInfo->encryptionSupport = ENCRYPTION_FULL_DISK;
        }
        if (wordPtr[48] & BIT0)
        {
            //TCG - SED drive (need to test a trusted command to see if it is being blocked or not)
            if (SUCCESS != ata_Trusted_Non_Data(device, 0, true, 0))
            {
                driveInfo->trustedCommandsBeingBlocked = true;
            }
            else
            {
                //Read supported security protocol list
                uint8_t *protocolList = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t));
                if (protocolList)
                {
                    if (SUCCESS == ata_Trusted_Receive(device, device->drive_info.ata_Options.dmaSupported, 0, 0, protocolList, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t listLength = M_BytesTo2ByteValue(protocolList[7], protocolList[6]);
                        for (uint16_t offset = 8; offset < (listLength + 8) && offset < LEGACY_DRIVE_SEC_SIZE; ++offset)
                        {
                            switch (protocolList[offset])
                            {
                            case SECURITY_PROTOCOL_INFORMATION:
                                break;
                            case SECURITY_PROTOCOL_TCG_1:
                            case SECURITY_PROTOCOL_TCG_2:
                            case SECURITY_PROTOCOL_TCG_3:
                            case SECURITY_PROTOCOL_TCG_4:
                            case SECURITY_PROTOCOL_TCG_5:
                            case SECURITY_PROTOCOL_TCG_6:
                                driveInfo->encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "TCG");
                                driveInfo->numberOfFeaturesSupported++;
                                break;
                            case SECURITY_PROTOCOL_CbCS:
                            case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
                            case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
                            case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
                            case SECURITY_PROTOCOL_IKE_V2_SCSI:
                            case SECURITY_PROTOCOL_NVM_EXPRESS:
                                break;
                            case SECURITY_PROTOCOL_SCSA:
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCSA");
                                driveInfo->numberOfFeaturesSupported++;
                                break;
                            case SECURITY_PROTOCOL_JEDEC_UFS:
                            case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
                                break;
                            case SECURITY_PROTOCOL_IEEE_1667:
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "IEEE 1667");
                                driveInfo->numberOfFeaturesSupported++;
                                break;
                            case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD://T10 only (SAT)
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    safe_Free(protocolList);
                }
            }
        }
        //cache size (legacy method - from ATA 1/2)
        driveInfo->cacheSize = M_BytesTo2ByteValue(bytePtr[0x2B], bytePtr[0x2A]) * driveInfo->logicalSectorSize;
        if (transportType == 0xE)
        {
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PCIE;
        }
        else if (wordPtr[76] != 0 && wordPtr[76] != UINT16_MAX)
        {
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_SERIAL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            //port speed
            driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts = 1;
            driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0;
            //Word 76 holds bits for supporteed signalling speeds (SATA)
            if (wordPtr[76] & BIT3)
            {
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 3;
            }
            else if (wordPtr[76] & BIT2)
            {
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 2;
            }
            else if (wordPtr[76] & BIT1)
            {
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 1;
            }
            else
            {
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 0;
            }
            //Word 77 has a coded value for the negotiated speed.
            switch (M_Nibble0(wordPtr[77]) >> 1)
            {
            case 3://6.0Gb/s
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 3;
                break;
            case 2://3.0Gb/s
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 2;
                break;
            case 1://1.5Gb/s
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 1;
                break;
            case 0:
            default:
                driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 0;
                break;
            }
        }
        else
        {
            //parallel speed.
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            if (wordPtr[53] & BIT1)//UDMA modes
            {
                uint8_t supported = M_Byte0(wordPtr[88]);
                uint8_t selected = M_Byte1(wordPtr[88]);
                uint8_t counter = -1;
                while (supported > 0)
                {
                    supported = supported >> 1;
                    ++counter;
                }
                switch (counter)
                {
                case 7://compact flash only
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 167;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-7");
                    break;
                case 6:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 133;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-6");
                    break;
                case 5:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 100;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-5");
                    break;
                case 4:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 66.7;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-4");
                    break;
                case 3:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 44.4;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-3");
                    break;
                case 2:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 33.3;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-2");
                    break;
                case 1:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 25;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-1");
                    break;
                case 0:
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "UDMA-0");
                    break;
                }
                //now check selected
                if (selected > 0)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                    counter = -1;
                    while (selected > 0)
                    {
                        selected = selected >> 1;
                        ++counter;
                    }
                    switch (counter)
                    {
                    case 7://compact flash only
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 167;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-7");
                        break;
                    case 6:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 133;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-6");
                        break;
                    case 5:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 100;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-5");
                        break;
                    case 4:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 66.7;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-4");
                        break;
                    case 3:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 44.4;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-3");
                        break;
                    case 2:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 33.3;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-2");
                        break;
                    case 1:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 25;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-1");
                        break;
                    case 0:
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 16.7;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "UDMA-0");
                        break;
                    }
                }
            }
            uint8_t counter = -1;
            //MWDMA
            uint8_t mwdmaSupported = M_GETBITRANGE(wordPtr[63], 2, 0);
            uint8_t mwdmaSelected = M_GETBITRANGE(wordPtr[63], 10, 8);
            if (mwdmaSupported > 0  && mwdmaSupported < UINT8_MAX)
            {
                while (mwdmaSupported > 0)
                {
                    mwdmaSupported = mwdmaSupported >> 1;
                    ++counter;
                }
                switch (counter)
                {
                case 2:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "MWDMA-2");
                    }
                    break;
                case 1:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 13.3)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 13.3;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "MWDMA-1");
                    }
                    break;
                case 0:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "MWDMA-0");
                    }
                    break;
                }
                //now check selected
                if (mwdmaSelected > 0)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                    counter = -1;
                    while (mwdmaSelected > 0)
                    {
                        mwdmaSelected = mwdmaSelected >> 1;
                        ++counter;
                    }
                    switch (counter)
                    {
                    case 2:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 16.7)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 16.7;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "MWDMA-2");
                        }
                        break;
                    case 1:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 13.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 13.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "MWDMA-1");
                        }
                        break;
                    case 0:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 4.2)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 4.2;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "MWDMA-0");
                        }
                        break;
                    }
                }
            }
            //SWDMA (obsolete since MW is so much faster...) (word 52 also holds the max supported value, but is also long obsolete...it can be checked if word 62 is not supported)
            uint8_t swdmaSupported = M_GETBITRANGE(wordPtr[62], 2, 0);
            uint8_t swdmaSelected = M_GETBITRANGE(wordPtr[62], 10, 8);
            bool swdmaWordSupported = false;
            if (swdmaSupported > 0 && swdmaSupported < UINT8_MAX)
            {
                counter = -1;
                swdmaWordSupported = true;
                while (swdmaSupported > 0)
                {
                    swdmaSupported = swdmaSupported >> 1;
                    ++counter;
                }
                switch (counter)
                {
                case 2:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-2");
                    }
                    break;
                case 1:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-1");
                    }
                    break;
                case 0:
                    if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 2.1)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 2.1;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                        sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-0");
                    }
                    break;
                }
                //now check selected
                if (swdmaSelected > 0)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                    counter = -1;
                    while (swdmaSelected > 0)
                    {
                        swdmaSelected = swdmaSelected >> 1;
                        ++counter;
                    }
                    switch (counter)
                    {
                    case 2:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 8.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 8.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "SWDMA-2");
                        }
                        break;
                    case 1:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 4.2)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 4.2;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "SWDMA-1");
                        }
                        break;
                    case 0:
                        if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 2.1)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 2.1;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "SWDMA-0");
                        }
                        break;
                    }
                }
            }
            uint16_t maxPIOCycleTime = 0;
            if (wordPtr[53] & BIT0)//other PIO modes
            {
                //PIO - from cycle time & mode3/4 support bits
                uint8_t advancedPIOModes = M_GETBITRANGE(wordPtr[64], 1, 0);
                
                if (advancedPIOModes)
                {
                    if (advancedPIOModes & BIT1)
                    {
                        maxPIOCycleTime = 120;
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-4");
                        }
                    }
                    else if (advancedPIOModes & BIT0)
                    {
                        maxPIOCycleTime = 180;
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 11.1)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 11.1;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-3");
                        }
                    }
                }
                else
                {
                    //determine maximum from cycle times?
                    maxPIOCycleTime = wordPtr[68];
                    switch (maxPIOCycleTime)
                    {
                    case 120://PIO4
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-4");
                        }
                        break;
                    case 180://PIO3
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 11.1)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 11.1;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-3");
                        }
                        break;
                    case 240://PIO2
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-2");
                        }
                        break;
                    case 383://PIO1
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 5.2)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.2;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-1");
                        }
                        break;
                    case 600://PIO0
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 3.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 3.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-0");
                        }
                        break;
                    }
                }
            }
            else
            {
                if (!swdmaWordSupported && wordPtr[52] != UINT16_MAX)
                {
                    switch (wordPtr[52])
                    {
                    case 2:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-2");
                        }
                        break;
                    case 1:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-1");
                        }
                        break;
                    case 0:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 2.1)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 2.1;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "SWDMA-0");
                        }
                        break;
                    }
                }
                //if still nothing, word 51 is obsolete (ATA4) but contains the PIO mode (0 - 3). Not sure if this is a "supported" or "negotiated" value.
                if (wordPtr[51] != UINT16_MAX)
                {
                    switch (wordPtr[52])
                    {
                    case 2:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-2");
                        }
                        break;
                    case 1:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 5.2)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.2;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-1");
                        }
                        break;
                    case 0:
                        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 3.3)
                        {
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 3.3;
                            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "PIO-0");
                        }
                        break;
                    }
                }
                //if PIO mode 0, we also need to check some even older bits for Mb/s for ancient history
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed <= 3.3)
                {
                    //check if the really OLD Mb/s bits are set...if they are, set the speed based off of them
                    if (M_GETBITRANGE(wordPtr[0], 10, 8) > 0)
                    {
                        memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
                        driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_ANCIENT;
                        driveInfo->interfaceSpeedInfo.speedIsValid = true;
                        if (wordPtr[0] & BIT10)
                        {
                            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt10MbS = true;
                        }
                        if (wordPtr[0] & BIT9)
                        {
                            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt5MbSLte10MbS = true;
                        }
                        if (wordPtr[0] & BIT8)
                        {
                            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferLte5MbS = true;
                        }
                        if (wordPtr[0] & BIT3)
                        {
                            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.notMFMEncoded = true;
                        }
                    }

                }
            }
        }

        //firmware download support
        if (wordPtr[69] & BIT8)
        {
            driveInfo->fwdlSupport.dmaModeSupported = true;
        }
        if (wordPtr[83] & BIT0 || wordPtr[86] & BIT0)
        {
            driveInfo->fwdlSupport.downloadSupported = true;
        }
        if (wordPtr[119] & BIT4 || wordPtr[120] & BIT4)
        {
            driveInfo->fwdlSupport.segmentedSupported = true;
        }
        if (is_Seagate_Family(device) == SEAGATE && wordPtr[243] & BIT12)
        {
            driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired = true;
        }
        //ata security status
        get_ATA_Security_Info(device, &driveInfo->ataSecurityInformation, false);
        //read look ahead
        if (wordPtr[82] & BIT6)
        {
            driveInfo->readLookAheadSupported = true;
            if (wordPtr[85] & BIT6)
            {
                driveInfo->readLookAheadEnabled = true;
            }
        }
        //write cache
        if (wordPtr[82] & BIT5)
        {
            driveInfo->writeCacheSupported = true;
            if (wordPtr[85] & BIT5)
            {
                driveInfo->writeCacheEnabled = true;
            }
        }
        //NV Cache Size logical blocks - needs testing against different drives to make sure the value is correct
        driveInfo->hybridNANDSize = M_WordsTo4ByteValue(wordPtr[215], wordPtr[216]) * driveInfo->logicalSectorSize;
        //create a list of supported features
        if (driveInfo->trustedCommandsBeingBlocked == true)
        {
            if (wordPtr[48] & BIT0)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "TCG");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (wordPtr[69] & BIT7)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "IEEE 1667");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[59] & BIT12)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Sanitize");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[76] != 0 && wordPtr[76] != 0xFFFF && wordPtr[76] & BIT8)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA NCQ");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[77] != 0 && wordPtr[77] != 0xFFFF && wordPtr[77] & BIT4)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA NCQ Streaming");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT11)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT11)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Rebuild Assist [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Rebuild Assist");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT9)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT9)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Hybrid Information [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Hybrid Information");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT8)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT8)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Device Sleep [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Device Sleep Preservation");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT6)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT6)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Software Settings Preservation [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Software Settings Preservation");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT5)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT5)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Hardware Feature Control [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Hardware Feature Control");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT4)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT4)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA In-Order Data Delivery [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA In-Order Data Delivery");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[78] != 0 && wordPtr[78] != 0xFFFF && wordPtr[78] & BIT3)
        {
            if (wordPtr[79] != 0 && wordPtr[79] != 0xFFFF && wordPtr[79] & BIT3)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Device Initiated Power Management [Enabled]");
                driveInfo->numberOfFeaturesSupported++;
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SATA Device Initiated Power Management");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (wordPtr[82] & BIT10 || wordPtr[85] & BIT10)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "HPA");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[82] & BIT3)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Power Management");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[82] & BIT1)
        {
            if (wordPtr[85] & BIT1)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Security [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Security");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[82] & BIT0)
        {
            if (wordPtr[85] & BIT0)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SMART [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SMART");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT11 || wordPtr[86] & BIT11)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "DCO");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT10 || wordPtr[86] & BIT10)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "48bit Address");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT9)
        {
            if (wordPtr[86] & BIT9)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "AAM [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "AAM");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT5)
        {
            if (wordPtr[86] & BIT5)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "PUIS [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "PUIS");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT4)
        {
            if (wordPtr[86] & BIT4)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Removable Media Status Notification [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Removable Media Status Notification");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT3)
        {
            if (wordPtr[86] & BIT3)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "APM [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "APM");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT2)
        {
            if (wordPtr[86] & BIT2)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "CFA [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "CFA");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[83] & BIT1 || wordPtr[86] & BIT1)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "TCQ");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[84] & BIT5 || wordPtr[86] & BIT5)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "GPL");
            driveInfo->numberOfFeaturesSupported++;
            gplSupported = true;
        }
        if (wordPtr[84] & BIT4)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Streaming");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[84] & BIT3 || wordPtr[87] & BIT3)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Media Card Pass-through");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[84] & BIT1 || wordPtr[87] & BIT1)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SMART Self-Test");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[84] & BIT0 || wordPtr[87] & BIT0)
        {
            smartErrorLoggingSupported = true;
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SMART Error Logging");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[82] & BIT4 || wordPtr[85] & BIT4)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Packet");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT5)
        {
            if (wordPtr[120] & BIT5)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Free-fall Control [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Free-fall Control");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT1)
        {
            if (wordPtr[120] & BIT1)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Write-Read-Verify [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Write-Read-Verify");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT9)
        {
            if (wordPtr[120] & BIT9)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "DSN [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "DSN");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT8)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "AMAC");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT7)
        {
            if (wordPtr[120] & BIT7)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "EPC [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "EPC");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[119] & BIT6)
        {
            if (wordPtr[120] & BIT6)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Sense Data Reporting [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Sense Data Reporting");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[169] & BIT0)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "TRIM");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[206] & BIT0)
        {
            sctSupported = true;
            if (wordPtr[206] & BIT1)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCT Read/Write Long");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (wordPtr[206] & BIT2)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCT Write Same");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (wordPtr[206] & BIT3)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCT Error Recovery Control");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (wordPtr[206] & BIT4)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCT Feature Control");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (wordPtr[206] & BIT5)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCT Data Tables");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        if (M_Byte3(wordPtr[214]) > 0)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "NV Cache");
            driveInfo->numberOfFeaturesSupported++;
        }
        if (wordPtr[214] & BIT0)
        {
            if (wordPtr[214] & BIT1)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "NV Cache Power Mode [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "NV Cache Power Mode");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
    }
    driveInfo->percentEnduranceUsed = -1;//start with this to filter out this value later if necessary

    //Read Log data
    uint32_t logBufferSize = LEGACY_DRIVE_SEC_SIZE;
    uint8_t *logBuffer = (uint8_t*)calloc(logBufferSize, sizeof(uint8_t));
    if (!logBuffer)
    {
        return MEMORY_FAILURE;
    }
    
    bool gotLogDirectory = false;
    bool directoryFromGPL = false;
    if (gplSupported && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
    {
        gotLogDirectory = true;
        directoryFromGPL = true;
    }
    else if (smartErrorLoggingSupported && SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_DIRECTORY, logBuffer, LEGACY_DRIVE_SEC_SIZE))
    {
        gotLogDirectory = true;
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }

    if (gotLogDirectory)
    {
        //check for log sizes we are interested in
        uint32_t devStatsSize = 0, idDataLog = 0, hybridInfoSize = 0, smartSelfTest = 0, extSelfTest = 0, hostlogging = 0, sctStatus = 0;
        devStatsSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2) + 1], logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        idDataLog = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2) + 1], logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        hybridInfoSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_HYBRID_INFORMATION * 2) + 1], logBuffer[(ATA_LOG_HYBRID_INFORMATION * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        smartSelfTest = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_SMART_SELF_TEST_LOG * 2) + 1], logBuffer[(ATA_LOG_SMART_SELF_TEST_LOG * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        extSelfTest = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG * 2) + 1], logBuffer[(ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        sctStatus = M_BytesTo2ByteValue(logBuffer[(ATA_SCT_COMMAND_STATUS * 2) + 1], logBuffer[(ATA_SCT_COMMAND_STATUS * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        hostlogging = M_BytesTo2ByteValue(logBuffer[(0x80 * 2) + 1], logBuffer[(0x80 * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        if (hostlogging > 0)
        {
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Host Logging");
            driveInfo->numberOfFeaturesSupported++;
        }
        //read identify device data log (only some pages are needed)
        if (idDataLog > 0) //Can come from SMART or GPL! Need to handle this...
        {
            if (gplSupported && directoryFromGPL)
            {
                bool capacity = false;
                bool supportedCapabilities = false;
                bool currentSettings = false;
                if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_PAGES, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                {
                    uint8_t pageNumber = logBuffer[2];
                    uint16_t revision = M_BytesTo2ByteValue(logBuffer[1], logBuffer[0]);
                    if (pageNumber == (uint8_t)ATA_ID_DATA_LOG_SUPPORTED_PAGES && revision >= 0x0001)
                    {
                        //data is valid, so figure out supported pages
                        uint8_t listLen = logBuffer[8];
                        for (uint16_t iter = 9; iter < (uint16_t)(listLen + 8) && iter < UINT16_C(512); ++iter)
                        {
                            switch (logBuffer[iter])
                            {
                            case ATA_ID_DATA_LOG_SUPPORTED_PAGES:
                            case ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA:
                                break;
                            case ATA_ID_DATA_LOG_CAPACITY:
                                capacity = true;
                                break;
                            case ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES:
                                supportedCapabilities = true;
                                break;
                            case ATA_ID_DATA_LOG_CURRENT_SETTINGS:
                                currentSettings = true;
                                break;
                            case ATA_ID_DATA_LOG_ATA_STRINGS:
                            case ATA_ID_DATA_LOG_SECURITY:
                            case ATA_ID_DATA_LOG_PARALLEL_ATA:
                            case ATA_ID_DATA_LOG_SERIAL_ATA:
                            case ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION:
                            default:
                                break;
                            }
                        }
                    }
                }
                //we need to read pages 2, 3 (read them one at a time to work around some USB issues as best we can)
                if (capacity && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 2, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_CAPACITY && M_Word0(qword0) >= 0x0001)
                    {
                        //get the nominal buffer size
                        uint64_t nominalBufferSize = M_BytesTo8ByteValue(logBuffer[39], logBuffer[38], logBuffer[37], logBuffer[36], logBuffer[35], logBuffer[34], logBuffer[33], logBuffer[32]);
                        if (nominalBufferSize & BIT63)
                        {
                            //data is valid. Remove bit 63
                            nominalBufferSize ^= BIT63;
                            //now save this value to cache size (number of bytes)
                            driveInfo->cacheSize = nominalBufferSize;
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                bool dlcSupported = false;
                if (supportedCapabilities && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 3, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    //supported capabilities
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES && M_Word0(qword0) >= 0x0001)
                    {
                        uint64_t supportedCapabilities = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (supportedCapabilities & BIT63)
                        {
                            if (supportedCapabilities & BIT54)
                            {
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Advanced Background Operations");
                                driveInfo->numberOfFeaturesSupported++;
                            }
                            if (supportedCapabilities & BIT49)
                            {
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Set Sector Configuration");
                                driveInfo->numberOfFeaturesSupported++;
                            }
                            if (supportedCapabilities & BIT46)
                            {
                                dlcSupported = true;
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Device Life Control");
                                driveInfo->numberOfFeaturesSupported++;
                            }
                        }
                        //Download capabilities
                        uint64_t downloadCapabilities = M_BytesTo8ByteValue(logBuffer[23], logBuffer[22], logBuffer[21], logBuffer[20], logBuffer[19], logBuffer[18], logBuffer[17], logBuffer[16]);
                        if (downloadCapabilities & BIT63)
                        {
                            if (downloadCapabilities & BIT34)
                            {
                                driveInfo->fwdlSupport.deferredSupported = true;
                            }
                        }
                        //Utilization (IDK if we need anything from this log for this information)

                        //zoned capabilities
                        uint64_t zonedCapabilities = M_BytesTo8ByteValue(logBuffer[111], logBuffer[110], logBuffer[109], logBuffer[108], logBuffer[107], logBuffer[106], logBuffer[105], logBuffer[104]);
                        if (zonedCapabilities & BIT63)
                        {
                            //we only need bits 1 & 2
                            driveInfo->zonedDevice = zonedCapabilities & 0x3;
                        }

                        //depopulate storage element support
                        uint64_t supportedCapabilitiesQWord18 = M_BytesTo8ByteValue(logBuffer[159], logBuffer[158], logBuffer[157], logBuffer[156], logBuffer[155], logBuffer[154], logBuffer[153], logBuffer[152]);
                        if (supportedCapabilitiesQWord18 & BIT63)//making sure this is set for "validity"
                        {
                            if (supportedCapabilitiesQWord18 & BIT1 && supportedCapabilitiesQWord18 & BIT0)//checking for both commands to be supported
                            {
                                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Storage Element Depopulation");
                                driveInfo->numberOfFeaturesSupported++;
                            }
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                bool dlcEnabled = false;
                if(currentSettings && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 5, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_CURRENT_SETTINGS && M_Word0(qword0) >= 0x0001)
                    {
                        uint64_t currentSettings = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (currentSettings & BIT63)
                        {
                            if (currentSettings & BIT17)
                            {
                                dlcEnabled = true;
                            }
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                if (dlcSupported)
                {
                    if (dlcEnabled)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Device Life Control [Enabled]");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    else
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Device Life Control");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                }
            }
            else if (smartErrorLoggingSupported)
            {
                //First try reading only the first sector and check if the page we want is available
                if (ata_SMART_Read_Log(device, ATA_LOG_IDENTIFY_DEVICE_DATA, logBuffer, LEGACY_DRIVE_SEC_SIZE))
                {
                    uint64_t header = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (M_Word0(header) == 0x0001 && M_Byte2(header) == 0) //check page and version number
                    {
                        bool readFullLog = false;
                        //check that pages 2 and/or 3 are supported
                        uint16_t numberOfEntries = logBuffer[8];
                        for (uint16_t offset = 9; offset < (numberOfEntries + UINT8_C(8)) && offset < 512; ++offset)
                        {
                            if (logBuffer[offset] == 0x02 || logBuffer[offset] == 0x03)
                            {
                                readFullLog = true;
                                break;
                            }
                        }
                        if (readFullLog)
                        {
                            uint8_t *temp = (uint8_t*)realloc(logBuffer, idDataLog * sizeof(uint8_t));
                            if (temp)
                            {
                                logBuffer = temp;
                                logBufferSize = idDataLog * sizeof(uint8_t);
                                if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_IDENTIFY_DEVICE_DATA, logBuffer, logBufferSize))
                                {
                                    bool dlcSupported = false;
                                    bool dlcEnabled = false;
                                    //start att offset 1024 snce page 0 and page 1 are not needed. (0 = list of supported pages, 1 = copy of identify data)
                                    for (uint32_t offset = UINT32_C(1024); offset < logBufferSize; offset += UINT32_C(512))
                                    {
                                        uint64_t pageHeader = M_BytesTo8ByteValue(logBuffer[offset + 7], logBuffer[offset + 6], logBuffer[offset + 5], logBuffer[offset + 4], logBuffer[offset + 3], logBuffer[offset + 2], logBuffer[offset + 1], logBuffer[offset + 0]);
                                        if (pageHeader & BIT63 && M_Word0(pageHeader) >= 0x0001 && M_Byte2(pageHeader) == ATA_ID_DATA_LOG_CAPACITY) //check page and version number
                                        {
                                            //read page 2 data
                                            //get the nominal buffer size
                                            uint64_t nominalBufferSize = M_BytesTo8ByteValue(logBuffer[offset + 39], logBuffer[offset + 38], logBuffer[offset + 37], logBuffer[offset + 36], logBuffer[offset + 35], logBuffer[offset + 34], logBuffer[offset + 33], logBuffer[offset + 32]);
                                            if (nominalBufferSize & BIT63)
                                            {
                                                //data is valid. Remove bit 63
                                                nominalBufferSize ^= BIT63;
                                                //now save this value to cache size (number of bytes)
                                                driveInfo->cacheSize = nominalBufferSize;
                                            }
                                        }
                                        else if (pageHeader & BIT63 && M_Word0(pageHeader) >= 0x0001 && M_Byte2(pageHeader) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES) //check page and version number
                                        {
                                            //read page 3 data
                                            //supported capabilities
                                            uint64_t supportedCapabilities = M_BytesTo8ByteValue(logBuffer[offset + 15], logBuffer[offset + 14], logBuffer[offset + 13], logBuffer[offset + 12], logBuffer[offset + 11], logBuffer[offset + 10], logBuffer[offset + 9], logBuffer[offset + 8]);
                                            if (supportedCapabilities & BIT63)
                                            {
                                                if (supportedCapabilities & BIT54)
                                                {
                                                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Advanced Background Operations");
                                                    driveInfo->numberOfFeaturesSupported++;
                                                }
                                                if (supportedCapabilities & BIT49)
                                                {
                                                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Set Sector Configuration");
                                                    driveInfo->numberOfFeaturesSupported++;
                                                }
                                                if (supportedCapabilities & BIT46)
                                                {
                                                    dlcSupported = true;
                                                }
                                            }
                                            //Download capabilities
                                            uint64_t downloadCapabilities = M_BytesTo8ByteValue(logBuffer[offset + 23], logBuffer[offset + 22], logBuffer[offset + 21], logBuffer[offset + 20], logBuffer[offset + 19], logBuffer[offset + 18], logBuffer[offset + 17], logBuffer[offset + 16]);
                                            if (downloadCapabilities & BIT63)
                                            {
                                                if (downloadCapabilities & BIT34)
                                                {
                                                    driveInfo->fwdlSupport.deferredSupported = true;
                                                }
                                            }
                                            //Utilization (IDK if we need anything from this log for this information)

                                            //zoned capabilities
                                            uint64_t zonedCapabilities = M_BytesTo8ByteValue(logBuffer[offset + 111], logBuffer[offset + 110], logBuffer[offset + 109], logBuffer[offset + 108], logBuffer[offset + 107], logBuffer[offset + 106], logBuffer[offset + 105], logBuffer[offset + 104]);
                                            if (zonedCapabilities & BIT63)
                                            {
                                                //we only need bits 1 & 2
                                                driveInfo->zonedDevice = zonedCapabilities & 0x3;
                                            }

                                            //depopulate storage element support
                                            uint64_t supportedCapabilitiesQWord18 = M_BytesTo8ByteValue(logBuffer[offset + 159], logBuffer[offset + 158], logBuffer[offset + 157], logBuffer[offset + 156], logBuffer[offset + 155], logBuffer[offset + 154], logBuffer[offset + 153], logBuffer[offset + 152]);
                                            if (supportedCapabilitiesQWord18 & BIT63)//making sure this is set for "validity"
                                            {
                                                if (supportedCapabilitiesQWord18 & BIT1 && supportedCapabilitiesQWord18 & BIT0)//checking for both commands to be supported
                                                {
                                                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Storage Element Depopulation");
                                                    driveInfo->numberOfFeaturesSupported++;
                                                }
                                            }
                                        }
                                        else if (pageHeader & BIT63 && M_Word0(pageHeader) == 0x0001 && M_Byte2(pageHeader) == ATA_ID_DATA_LOG_CURRENT_SETTINGS) //check page and version number
                                        {
                                            //supported capabilities
                                            uint64_t currentSettings = M_BytesTo8ByteValue(logBuffer[offset + 15], logBuffer[offset + 14], logBuffer[offset + 13], logBuffer[offset + 12], logBuffer[offset + 11], logBuffer[offset + 10], logBuffer[offset + 9], logBuffer[offset + 8]);
                                            if (currentSettings & BIT63)
                                            {
                                                if (currentSettings & BIT17)
                                                {
                                                    dlcEnabled = true;
                                                }
                                            }
                                        }
                                    }
                                    if (dlcSupported)
                                    {
                                        if (dlcEnabled)
                                        {
                                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Device Life Control [Enabled]");
                                            driveInfo->numberOfFeaturesSupported++;
                                        }
                                        else
                                        {
                                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Device Life Control");
                                            driveInfo->numberOfFeaturesSupported++;
                                        }
                                    }
                                }
                                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                                {
                                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                                    scsi_Test_Unit_Ready(device, NULL);
                                }
                            }
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
        }
        //read device statistics log (only some pages are needed)
        if (devStatsSize > 0)//can come from GPL or SMART
        {
            if (gplSupported && directoryFromGPL)
            {
                //we need to read pages 1, 7, 5 (read them one at a time to work around some USB issues as best we can)
                //get list of supported pages first to use that to find supported pages we are interested in.
                bool generalStatistics = false;
                bool solidStateStatistics = false;
                bool temperatureStatistics = false;
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_LIST, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint16_t iter = 9;
                    uint8_t numberOfEntries = logBuffer[8];
                    for (iter = 9; iter < (numberOfEntries + 9) && iter < 512; ++iter)
                    {
                        switch (logBuffer[iter])
                        {
                        case ATA_DEVICE_STATS_LOG_LIST:
                            break;
                        case ATA_DEVICE_STATS_LOG_GENERAL:
                            generalStatistics = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_FREE_FALL:
                            break;
                        case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
                            break;
                        case ATA_DEVICE_STATS_LOG_GEN_ERR:
                            break;
                        case ATA_DEVICE_STATS_LOG_TEMP:
                            temperatureStatistics = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_TRANSPORT:
                            break;
                        case ATA_DEVICE_STATS_LOG_SSD:
                            solidStateStatistics = true;
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (generalStatistics && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GENERAL, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_GENERAL && M_Word0(qword0) >= 0x0001)//validating we got the right page
                    {
                        //power on hours
                        uint64_t pohQword = M_BytesTo8ByteValue(logBuffer[23], logBuffer[22], logBuffer[21], logBuffer[20], logBuffer[19], logBuffer[18], logBuffer[17], logBuffer[16]);
                        if (pohQword & BIT63 && pohQword & BIT62)
                        {
                            driveInfo->powerOnMinutes = M_DoubleWord0(pohQword) * 60;
                        }
                        //logical sectors written
                        uint64_t lsWrittenQword = M_BytesTo8ByteValue(logBuffer[31], logBuffer[30], logBuffer[29], logBuffer[28], logBuffer[27], logBuffer[26], logBuffer[25], logBuffer[24]);
                        if (lsWrittenQword & BIT63 && lsWrittenQword & BIT62)
                        {
                            driveInfo->totalLBAsWritten = lsWrittenQword & MAX_48_BIT_LBA;
                        }
                        //logical sectors read
                        uint64_t lsReadQword = M_BytesTo8ByteValue(logBuffer[47], logBuffer[46], logBuffer[45], logBuffer[44], logBuffer[43], logBuffer[42], logBuffer[41], logBuffer[40]);
                        if (lsReadQword & BIT63 && lsReadQword & BIT62)
                        {
                            driveInfo->totalLBAsRead = lsReadQword & MAX_48_BIT_LBA;
                        }
                        //workload utilization
                        uint64_t worloadUtilization = M_BytesTo8ByteValue(logBuffer[79], logBuffer[78], logBuffer[77], logBuffer[76], logBuffer[75], logBuffer[74], logBuffer[73], logBuffer[72]);
                        if (worloadUtilization & BIT63 && worloadUtilization & BIT62)
                        {
                            driveInfo->deviceReportedUtilizationRate = ((double)M_Word0(worloadUtilization)) / 1000.0;
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                if (temperatureStatistics && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_TEMP, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_TEMP && M_Word0(qword0) >= 0x0001)//validating we got the right page
                    {
                        //current temperature
                        uint64_t currentTemp = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (currentTemp & BIT63 && currentTemp & BIT62)
                        {
                            driveInfo->temperatureData.temperatureDataValid = true;
                            driveInfo->temperatureData.currentTemperature = M_Byte0(currentTemp);
                        }
                        //highest temperature
                        uint64_t highestTemp = M_BytesTo8ByteValue(logBuffer[39], logBuffer[38], logBuffer[37], logBuffer[36], logBuffer[35], logBuffer[34], logBuffer[33], logBuffer[32]);
                        if (highestTemp & BIT63 && highestTemp & BIT62)
                        {
                            driveInfo->temperatureData.highestTemperature = M_Byte0(highestTemp);
                            driveInfo->temperatureData.highestValid = true;
                        }
                        //lowest temperature
                        uint64_t lowestTemp = M_BytesTo8ByteValue(logBuffer[47], logBuffer[46], logBuffer[45], logBuffer[44], logBuffer[43], logBuffer[42], logBuffer[41], logBuffer[40]);
                        if (lowestTemp & BIT63 && lowestTemp & BIT62)
                        {
                            driveInfo->temperatureData.lowestTemperature = M_Byte0(lowestTemp);
                            driveInfo->temperatureData.lowestValid = true;
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                if (solidStateStatistics && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_SSD, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_SSD && M_Word0(qword0) >= 0x0001)//validating we got the right page
                    {
                        //percent used endurance
                        uint64_t percentUsed = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (percentUsed & BIT63 && percentUsed & BIT62)
                        {
                            driveInfo->percentEnduranceUsed = M_Byte0(percentUsed);
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
            else if (smartErrorLoggingSupported)
            {
                //read first page for a list if supported pages
                if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_DEVICE_STATISTICS, logBuffer, LEGACY_DRIVE_SEC_SIZE))
                {
                    uint64_t header = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (M_Word0(header) == 0x0001 && M_Byte2(header) == 0) //check page and version number
                    {
                        bool readFullLog = false;
                        //check that pages 1, 5, and/or 7 are supported
                        uint16_t numberOfEntries = logBuffer[8];
                        for (uint16_t offset = 9; offset < (numberOfEntries + UINT8_C(8)); ++offset)
                        {
                            if (logBuffer[offset] == ATA_DEVICE_STATS_LOG_GENERAL || logBuffer[offset] == ATA_DEVICE_STATS_LOG_TEMP || logBuffer[offset] == ATA_DEVICE_STATS_LOG_SSD)
                            {
                                readFullLog = true;
                                break;
                            }
                        }
                        if (readFullLog)
                        {
                            uint8_t *temp = (uint8_t*)realloc(logBuffer, devStatsSize * sizeof(uint8_t));
                            if (temp)
                            {
                                logBuffer = temp;
                                logBufferSize = devStatsSize * sizeof(uint8_t);
                                if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_DEVICE_STATISTICS, logBuffer, logBufferSize))
                                {
                                    for (uint32_t offset = 0; offset < logBufferSize; offset += UINT32_C(512))
                                    {
                                        uint16_t revision = 0;
                                        uint8_t pageNumber = 0;
                                        header = M_BytesTo8ByteValue(logBuffer[offset + 7], logBuffer[offset + 6], logBuffer[offset + 5], logBuffer[offset + 4], logBuffer[offset + 3], logBuffer[offset + 2], logBuffer[offset + 1], logBuffer[offset + 0]);
                                        revision = M_Word0(header);
                                        pageNumber = M_Byte2(header);
                                        switch (pageNumber)
                                        {
                                        case ATA_DEVICE_STATS_LOG_GENERAL:
                                            if (revision >= 0x0001)
                                            {
                                                //power on hours
                                                uint64_t pohQword = M_BytesTo8ByteValue(logBuffer[offset + 23], logBuffer[offset + 22], logBuffer[offset + 21], logBuffer[offset + 20], logBuffer[offset + 19], logBuffer[offset + 18], logBuffer[offset + 17], logBuffer[offset + 16]);
                                                if (pohQword & BIT63 && pohQword & BIT62)
                                                {
                                                    driveInfo->powerOnMinutes = M_DoubleWord0(pohQword) * 60;
                                                }
                                                //logical sectors written
                                                uint64_t lsWrittenQword = M_BytesTo8ByteValue(logBuffer[offset + 31], logBuffer[offset + 30], logBuffer[offset + 29], logBuffer[offset + 28], logBuffer[offset + 27], logBuffer[offset + 26], logBuffer[offset + 25], logBuffer[offset + 24]);
                                                if (lsWrittenQword & BIT63 && lsWrittenQword & BIT62)
                                                {
                                                    driveInfo->totalLBAsWritten = lsWrittenQword & MAX_48_BIT_LBA;
                                                }
                                                //logical sectors read
                                                uint64_t lsReadQword = M_BytesTo8ByteValue(logBuffer[offset + 47], logBuffer[offset + 46], logBuffer[offset + 45], logBuffer[offset + 44], logBuffer[offset + 43], logBuffer[offset + 42], logBuffer[offset + 41], logBuffer[offset + 40]);
                                                if (lsReadQword & BIT63 && lsReadQword & BIT62)
                                                {
                                                    driveInfo->totalLBAsRead = lsReadQword & MAX_48_BIT_LBA;
                                                }
                                                //workload utilization
                                                uint64_t worloadUtilization = M_BytesTo8ByteValue(logBuffer[offset + 79], logBuffer[offset + 78], logBuffer[offset + 77], logBuffer[offset + 76], logBuffer[offset + 75], logBuffer[offset + 74], logBuffer[offset + 73], logBuffer[offset + 72]);
                                                if (worloadUtilization & BIT63 && worloadUtilization & BIT62)
                                                {
                                                    driveInfo->deviceReportedUtilizationRate = ((double)M_Word0(worloadUtilization)) / 1000.0;
                                                }
                                            }
                                            break;
                                        case ATA_DEVICE_STATS_LOG_TEMP:
                                            if (revision >= 0x0001)
                                            {
                                                //current temperature
                                                uint64_t currentTemp = M_BytesTo8ByteValue(logBuffer[offset + 15], logBuffer[offset + 14], logBuffer[offset + 13], logBuffer[offset + 12], logBuffer[offset + 11], logBuffer[offset + 10], logBuffer[offset + 9], logBuffer[offset + 8]);
                                                if (currentTemp & BIT63 && currentTemp & BIT62)
                                                {
                                                    driveInfo->temperatureData.temperatureDataValid = true;
                                                    driveInfo->temperatureData.currentTemperature = M_Byte0(currentTemp);
                                                }
                                                //highest temperature
                                                uint64_t highestTemp = M_BytesTo8ByteValue(logBuffer[offset + 39], logBuffer[offset + 38], logBuffer[offset + 37], logBuffer[offset + 36], logBuffer[offset + 35], logBuffer[offset + 34], logBuffer[offset + 33], logBuffer[offset + 32]);
                                                if (highestTemp & BIT63 && highestTemp & BIT62)
                                                {
                                                    driveInfo->temperatureData.highestTemperature = M_Byte0(highestTemp);
                                                    driveInfo->temperatureData.highestValid = true;
                                                }
                                                //lowest temperature
                                                uint64_t lowestTemp = M_BytesTo8ByteValue(logBuffer[offset + 47], logBuffer[offset + 46], logBuffer[offset + 45], logBuffer[offset + 44], logBuffer[offset + 43], logBuffer[offset + 42], logBuffer[offset + 41], logBuffer[offset + 40]);
                                                if (lowestTemp & BIT63 && lowestTemp & BIT62)
                                                {
                                                    driveInfo->temperatureData.lowestTemperature = M_Byte0(lowestTemp);
                                                    driveInfo->temperatureData.lowestValid = true;
                                                }
                                            }
                                            break;
                                        case ATA_DEVICE_STATS_LOG_SSD:
                                            if (revision >= 0x0001)
                                            {
                                                //percent used endurance
                                                uint64_t percentUsed = M_BytesTo8ByteValue(logBuffer[offset + 15], logBuffer[offset + 14], logBuffer[offset + 13], logBuffer[offset + 12], logBuffer[offset + 11], logBuffer[offset + 10], logBuffer[offset + 9], logBuffer[offset + 8]);
                                                if (percentUsed & BIT63 && percentUsed & BIT62)
                                                {
                                                    driveInfo->percentEnduranceUsed = M_Byte0(percentUsed);
                                                }
                                            }
                                            break;
                                        default://don't care about this page number right now
                                            break;
                                        }
                                    }
                                }
                                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                                {
                                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                                    scsi_Test_Unit_Ready(device, NULL);
                                }
                            }
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
        }
        if (gplSupported && hybridInfoSize > 0)//GPL only. Page is also only a size of 1 512B block
        {
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_HYBRID_INFORMATION, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                driveInfo->hybridNANDSize = M_BytesTo8ByteValue(logBuffer[23], logBuffer[22], logBuffer[21], logBuffer[20], logBuffer[19], logBuffer[18], logBuffer[17], logBuffer[16]) * driveInfo->logicalSectorSize;
            }
            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
        }
        if (gplSupported && extSelfTest > 0)//GPL only
        {
            memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
            //get the most recent result
            //read the first page to get the pointer to the most recent result, then if that's on another page, read that page
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                uint16_t selfTestIndex = M_BytesTo2ByteValue(logBuffer[3], logBuffer[2]);
                if (selfTestIndex > 0)
                {
                    uint8_t descriptorLength = 26;
                    uint16_t descriptorOffset = ((selfTestIndex * descriptorLength) - descriptorLength) + 4;
                    uint16_t pageNumber = descriptorOffset / LEGACY_DRIVE_SEC_SIZE;
                    bool mostRecentPageRead = true;
                    if (pageNumber > 0)
                    {
                        //adjust the offset for when we read the correct page
                        descriptorOffset = descriptorOffset - (LEGACY_DRIVE_SEC_SIZE * pageNumber);
                        if (SUCCESS != send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, pageNumber, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                        {
                            mostRecentPageRead = false;
                        }
                    }
                    //if we made it to here, our data buffer now has the log page that contains the descriptor we are looking for
                    if (mostRecentPageRead)
                    {
                        driveInfo->dstInfo.informationValid = true;
                        driveInfo->dstInfo.powerOnHours = M_BytesTo2ByteValue(logBuffer[descriptorOffset + 3], logBuffer[descriptorOffset + 2]);
                        driveInfo->dstInfo.resultOrStatus = M_Nibble1(logBuffer[descriptorOffset + 1]);
                        driveInfo->dstInfo.testNumber = logBuffer[descriptorOffset];
                        if (M_Nibble1(logBuffer[descriptorOffset + 1]) == 0x07)
                        {
                            //LBA is a valid entry
                            driveInfo->dstInfo.errorLBA = M_BytesTo8ByteValue(0, 0, \
                                logBuffer[descriptorOffset + 10], logBuffer[descriptorOffset + 9], \
                                logBuffer[descriptorOffset + 8], logBuffer[descriptorOffset + 7], \
                                logBuffer[descriptorOffset + 6], logBuffer[descriptorOffset + 5]);
                        }
                        else
                        {
                            driveInfo->dstInfo.errorLBA = UINT64_MAX;
                        }
                    }
                }
                else
                {
                    //set to all 0's and error LBA to all Fs
                    driveInfo->dstInfo.informationValid = true;
                    driveInfo->dstInfo.powerOnHours = 0;
                    driveInfo->dstInfo.resultOrStatus = 0;
                    driveInfo->dstInfo.testNumber = 0;
                    driveInfo->dstInfo.errorLBA = UINT64_MAX;
                }
            }
            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
        }
        else if (smartErrorLoggingSupported && smartSelfTest > 0)//SMART log only (only 1 sector in size)
        {
            //get the most recent result
            if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_SMART_SELF_TEST_LOG, logBuffer, LEGACY_DRIVE_SEC_SIZE))
            {
                uint8_t selfTestIndex = logBuffer[508];
                if (selfTestIndex > 0)
                {
                    uint8_t descriptorLength = 24;
                    uint8_t descriptorOffset = ((selfTestIndex * descriptorLength) - descriptorLength) + 2;
                    uint8_t executionStatusByte = logBuffer[descriptorOffset + 1];
                    driveInfo->dstInfo.informationValid = true;
                    driveInfo->dstInfo.powerOnHours = M_BytesTo2ByteValue(logBuffer[descriptorOffset + 3], logBuffer[descriptorOffset + 2]);
                    driveInfo->dstInfo.resultOrStatus = M_Nibble1(logBuffer[descriptorOffset + 1]);
                    driveInfo->dstInfo.testNumber = logBuffer[descriptorOffset];
                    if (M_Nibble1(executionStatusByte) == 0x07)
                    {
                        //LBA is a valid entry
                        driveInfo->dstInfo.errorLBA = (uint64_t)M_BytesTo4ByteValue(logBuffer[descriptorOffset + 8], logBuffer[descriptorOffset + 7], \
                            logBuffer[descriptorOffset + 6], logBuffer[descriptorOffset + 5]);
                    }
                    else
                    {
                        driveInfo->dstInfo.errorLBA = UINT64_MAX;
                    }
                }
                else
                {
                    //set to all 0's and error LBA to all Fs
                    driveInfo->dstInfo.informationValid = true;
                    driveInfo->dstInfo.powerOnHours = 0;
                    driveInfo->dstInfo.resultOrStatus = 0;
                    driveInfo->dstInfo.testNumber = 0;
                    driveInfo->dstInfo.errorLBA = UINT64_MAX;
                }
            }
            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
        }
        bool readSCTStatusWithSMARTCommand = sct_With_SMART_Commands(device);//USB hack
        if (sctSupported && sctStatus > 0)//GPL or SMART
        {
            memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
            //Read the SCT status log
            bool sctStatusRead = false;
            if (gplSupported && !readSCTStatusWithSMARTCommand && directoryFromGPL && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_SCT_COMMAND_STATUS, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                sctStatusRead = true;
            }
            else if (smartErrorLoggingSupported && SUCCESS == ata_SMART_Read_Log(device, ATA_SCT_COMMAND_STATUS, logBuffer, LEGACY_DRIVE_SEC_SIZE))
            {
                sctStatusRead = true;
            }
            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
            if (sctStatusRead)
            {
                uint16_t sctFormatVersion = M_BytesTo2ByteValue(logBuffer[1], logBuffer[0]);
                if (sctFormatVersion > 1)//cannot find spec for revision 1 of this log, but we'll keep this safe until I find it with this check
                {
                    if (!driveInfo->temperatureData.temperatureDataValid && logBuffer[200] != 0x80)
                    {
                        driveInfo->temperatureData.temperatureDataValid = true;
                        driveInfo->temperatureData.currentTemperature = (int16_t)logBuffer[200];
                    }
                    if (!driveInfo->temperatureData.highestValid && logBuffer[204] != 0x80)
                    {
                        driveInfo->temperatureData.highestTemperature = (int16_t)logBuffer[204];
                        driveInfo->temperatureData.highestValid = true;
                    }
                }
                if (sctFormatVersion > 2)
                {
                    //version 3 and higher report current, min, and max temperatures
                    //reading life min and max temperatures
                    if (!driveInfo->temperatureData.lowestValid && logBuffer[203] != 0x80)
                    {
                        driveInfo->temperatureData.lowestTemperature = (int16_t)logBuffer[203];
                        driveInfo->temperatureData.lowestValid = true;
                    }
                    uint16_t smartStatus = M_BytesTo2ByteValue(logBuffer[215], logBuffer[214]);
                    //SMART status
                    switch (smartStatus)
                    {
                    case 0xC24F:
                        smartStatusFromSCTStatusLog = true;
                        driveInfo->smartStatus = 0;
                        break;
                    case 0x2CF4:
                        smartStatusFromSCTStatusLog = true;
                        driveInfo->smartStatus = 1;
                        break;
                    default:
                        driveInfo->smartStatus = 2;
                        break;
                    }
                }
            }
        }
    }
    safe_Free(logBuffer);
    
    uint8_t smartData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
    {
        //get long DST time
        driveInfo->longDSTTimeMinutes = smartData[373];
        if (driveInfo->longDSTTimeMinutes == UINT8_MAX)
        {
            driveInfo->longDSTTimeMinutes = M_BytesTo2ByteValue(smartData[376], smartData[375]);
        }
        //read temperature (194), poh (9) for all, then read 241, 242, and 231 for Seagate only
        ataSMARTAttribute *currentAttribute = NULL;
        uint16_t smartIter = 0;
        eSeagateFamily seagateFamily = is_Seagate_Family(device);
        if (seagateFamily == SEAGATE)
        {
            //check IDD and Reman support
            bool iddSupported = false;
            bool remanSupported = false;
            if (smartData[0x1EE] & BIT0)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT1)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT2)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT3)
            {
                remanSupported = true;
            }

            //set features supported
            if (iddSupported)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Seagate In Drive Diagnostics (IDD)");
                driveInfo->numberOfFeaturesSupported++;
            }
            if (remanSupported)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Seagate Remanufacture");
                driveInfo->numberOfFeaturesSupported++;
            }
        }
        //first get the SMART attributes that we care about
        for (smartIter = 2; smartIter < 362; smartIter += 12)
        {
            currentAttribute = (ataSMARTAttribute *)&smartData[smartIter];
            switch (currentAttribute->attributeNumber)
            {
            case 9: //POH (This attribute seems shared between vendors)
            {
                uint32_t millisecondsSinceIncrement = M_BytesTo4ByteValue(0, currentAttribute->rawData[6], currentAttribute->rawData[5], currentAttribute->rawData[4]);
                uint32_t powerOnMinutes = M_BytesTo4ByteValue(currentAttribute->rawData[3], currentAttribute->rawData[2], currentAttribute->rawData[1], currentAttribute->rawData[0]) * 60;
                powerOnMinutes += (millisecondsSinceIncrement / 60000);//convert the milliseconds to minutes, then add that to the amount of time we already know
                if (driveInfo->powerOnMinutes < powerOnMinutes)
                {
                    driveInfo->powerOnMinutes = powerOnMinutes;
                }
            }
                break;
            case 194: //Temperature (This attribute seems shared between vendors)
                if (!driveInfo->temperatureData.temperatureDataValid)
                {
                    driveInfo->temperatureData.temperatureDataValid = true;
                    driveInfo->temperatureData.currentTemperature = (int16_t)M_BytesTo2ByteValue(currentAttribute->rawData[1], currentAttribute->rawData[0]);
                }
                if (!driveInfo->temperatureData.lowestValid)
                {
                    driveInfo->temperatureData.lowestTemperature = (int16_t)M_BytesTo2ByteValue(currentAttribute->rawData[5], currentAttribute->rawData[4]);
                    driveInfo->temperatureData.lowestValid = true;
                }
                if (!driveInfo->temperatureData.highestValid)
                {
                    driveInfo->temperatureData.highestTemperature = (int16_t)currentAttribute->worstEver;
                    driveInfo->temperatureData.highestValid = true;
                }
                break;
            case 231: //SSD Endurance
                if ((seagateFamily == SEAGATE || seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_C || seagateFamily == SEAGATE_VENDOR_F || seagateFamily == SEAGATE_VENDOR_G) && driveInfo->percentEnduranceUsed < 0)
                {
                    // SCSI was implemented first, and it returns a value where 0 means 100% spares left, ATA is the opposite,
                    // so we need to subtract our number from 100
                    // On SATA drives below, we had firmware reporting in the range of 0-255 instead of 0-100. Lets check for them and normalize the value so it can be reported
                    // Note that this is only for some firmwares...we should figure out a better way to do this
                    // Here's the list of the affected SATA model numbers:
                    // ST100FM0022 (SED - rare)
                    // ST100FM0012 (SED - rare)
                    // ST200FM0012 (SED - rare)
                    // ST400FM0012 (SED - rare)
                    // ST100FM0062
                    // ST200FM0052
                    // ST400FM0052
                    if ((strcmp(driveInfo->modelNumber, "ST100FM0022") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST100FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST200FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST400FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST100FM0062") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST200FM0052") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST400FM0052") == 0) &&
                        strcmp(driveInfo->firmwareRevision, "0004") == 0)
                    {
                        driveInfo->percentEnduranceUsed = 100 - ((currentAttribute->nominal * 100) / 255);
                    }
                    else
                    {
                        driveInfo->percentEnduranceUsed = 100 - currentAttribute->nominal;
                    }
                }
                break;
            case 233: //Lifetime Write to Flash (SSD)
                if (seagateFamily == SEAGATE_VENDOR_G || seagateFamily == SEAGATE_VENDOR_F)
                {
                    driveInfo->totalWritesToFlash = M_BytesTo8ByteValue(0, currentAttribute->rawData[6], currentAttribute->rawData[5], currentAttribute->rawData[4], currentAttribute->rawData[3], currentAttribute->rawData[2], currentAttribute->rawData[1], currentAttribute->rawData[0]);
                    //convert this to match what we're doing below since this is likely also in GiB written (BUT IDK BECAUSE IT ISN'T IN THE SMART SPEC!)
                    driveInfo->totalWritesToFlash = (driveInfo->totalWritesToFlash * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                }
                break;
            case 234: //Lifetime Write to Flash (SSD)
                if (seagateFamily == SEAGATE || (seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_B))
                {
                    driveInfo->totalWritesToFlash = M_BytesTo8ByteValue(0, currentAttribute->rawData[6], currentAttribute->rawData[5], currentAttribute->rawData[4], currentAttribute->rawData[3], currentAttribute->rawData[2], currentAttribute->rawData[1], currentAttribute->rawData[0]);
                    //convert this to match what we're doing below since this is likely also in GiB written (BUT IDK BECAUSE IT ISN'T IN THE SMART SPEC!)
                    driveInfo->totalWritesToFlash = (driveInfo->totalWritesToFlash * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                }
                break;
            case 241: //Total Bytes written (SSD) Total LBAs written (HDD)
                if ((seagateFamily == SEAGATE || seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_B || seagateFamily == SEAGATE_VENDOR_F || seagateFamily == SEAGATE_VENDOR_G) && driveInfo->totalLBAsWritten == 0)
                {
                    driveInfo->totalLBAsWritten = M_BytesTo8ByteValue(0, currentAttribute->rawData[6], currentAttribute->rawData[5], currentAttribute->rawData[4], currentAttribute->rawData[3], currentAttribute->rawData[2], currentAttribute->rawData[1], currentAttribute->rawData[0]);
                    if (seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_B || seagateFamily == SEAGATE_VENDOR_F)
                    {
                        //some Seagate SSD's report this as GiB written, so convert to LBAs
                        driveInfo->totalLBAsWritten = (driveInfo->totalLBAsWritten * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                    }
                }
                break;
            case 242: //Total Bytes read (SSD) Total LBAs read (HDD)
                if ((seagateFamily == SEAGATE || seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_B || seagateFamily == SEAGATE_VENDOR_F || seagateFamily == SEAGATE_VENDOR_G) && driveInfo->totalLBAsRead == 0)
                {
                    driveInfo->totalLBAsRead = M_BytesTo8ByteValue(0, currentAttribute->rawData[6], currentAttribute->rawData[5], currentAttribute->rawData[4], currentAttribute->rawData[3], currentAttribute->rawData[2], currentAttribute->rawData[1], currentAttribute->rawData[0]);
                    if (seagateFamily == SEAGATE_VENDOR_D || seagateFamily == SEAGATE_VENDOR_E || seagateFamily == SEAGATE_VENDOR_B || seagateFamily == SEAGATE_VENDOR_F)
                    {
                        //some Seagate SSD's report this as GiB read, so convert to LBAs
                        driveInfo->totalLBAsRead = (driveInfo->totalLBAsRead * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    //set total bytes read/written
    driveInfo->totalBytesRead = driveInfo->totalLBAsRead * driveInfo->logicalSectorSize;
    driveInfo->totalBytesWritten = driveInfo->totalLBAsWritten * driveInfo->logicalSectorSize;
    //get the native maxLBA
    int getMaxAddress = ata_Get_Native_Max_LBA(device, &driveInfo->nativeMaxLBA);
    if (getMaxAddress != SUCCESS && device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    bool skipSMARTCheckDueToTranslatorBug = !supports_ATA_Return_SMART_Status_Command(device);//USB hack
    if (!smartStatusFromSCTStatusLog && !skipSMARTCheckDueToTranslatorBug)
    {
        //SMART status
        switch (ata_SMART_Check(device, NULL))
        {
        case SUCCESS:
            driveInfo->smartStatus = 0;
            break;
        case FAILURE:
            driveInfo->smartStatus = 1;
            if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
            break;
        default:
            driveInfo->smartStatus = 2;
            if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
            break;
        }
    }
    else
    {
        driveInfo->smartStatus = 2;
    }
    if (is_Seagate_Family(device) == SEAGATE)
    {
        driveInfo->lowCurrentSpinupValid = true;
        driveInfo->lowCurrentSpinupViaSCT = is_SCT_Low_Current_Spinup_Supported(device);
        driveInfo->lowCurrentSpinupEnabled = is_Low_Current_Spin_Up_Enabled(device, driveInfo->lowCurrentSpinupViaSCT);
    }
    return ret;
}

int get_SCSI_Drive_Information(tDevice *device, ptrDriveInformationSAS_SATA driveInfo)
{
    int ret = SUCCESS;
    memset(driveInfo, 0, sizeof(driveInformationSAS_SATA));
    //start with standard inquiry data
    uint8_t version = 0;
    uint8_t peripheralQualifier = 0;
    uint8_t peripheralDeviceType = 0;
    uint8_t inquiryData[255] = { 0 };
    uint8_t responseFormat = 0;
    bool protectionSupported = false;
    bool isSCSI1drive = false;
    bool isSCSI2drive = false;
    bool isSEAGATEVendorID = false;//matches SCSI/SAS/FC for Seagate
    if (SUCCESS == scsi_Inquiry(device, inquiryData, 255, 0, false, false))
    {
        //copy the read data to the device struct
        memcpy(device->drive_info.scsiVpdData.inquiryData, inquiryData, INQ_RETURN_DATA_LENGTH);
        //now parse the data
        peripheralQualifier = (device->drive_info.scsiVpdData.inquiryData[0] & (BIT7 | BIT6 | BIT5)) >> 5;
        peripheralDeviceType = device->drive_info.scsiVpdData.inquiryData[0] & (BIT4 | BIT3 | BIT2 | BIT1 | BIT0);
        //Vendor ID
        memcpy(&driveInfo->vendorID, &device->drive_info.scsiVpdData.inquiryData[8], 8);
        //MN-product identification
        memcpy(driveInfo->modelNumber, &device->drive_info.scsiVpdData.inquiryData[16], 16);
        //FWRev
        memcpy(driveInfo->firmwareRevision, &device->drive_info.scsiVpdData.inquiryData[32], 4);
        //Version (SPC version device conforms to)
        version = device->drive_info.scsiVpdData.inquiryData[2];
        responseFormat = M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[3], 3, 0);
        switch (version)
        {
        case 0:
            /*sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "Does not conform to a SCSI standard");
            driveInfo->numberOfSpecificationsSupported++;
            break;*/
            //Note: Old SCSI/SPC standards may report multiple specifications supported in this byte
            //Codes 08-0Ch, 40-44h, 48-4Ch, & 88h-8Ch are skipped in there. These were valious ways to report ISO/ECMA/ANSI standard conformance seperately for the same SCSI-x specification.
            //New standards (SPC6 or whatever is next) may use these values and they should be used.
        case 0x81:
        case 0x01:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SCSI");
            driveInfo->numberOfSpecificationsSupported++;
            isSCSI1drive = true;
            version = 1;
            break;
        case 0x02:
        case 0x80:
        case 0x82:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SCSI-2");
            driveInfo->numberOfSpecificationsSupported++;
            isSCSI2drive = true;
            version = 2;
            break;
        case 0x83:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC");
            driveInfo->numberOfSpecificationsSupported++;
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SCSI-2");
            driveInfo->numberOfSpecificationsSupported++;
            isSCSI2drive = true;
            version = 3;
            break;
        case 0x84:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC-2");
            driveInfo->numberOfSpecificationsSupported++;
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SCSI-2");
            driveInfo->numberOfSpecificationsSupported++;
            isSCSI2drive = true;
            version = 4;
            break;
        case 0x03:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC");
            driveInfo->numberOfSpecificationsSupported++;
            break;
        case 0x04:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC-2");
            driveInfo->numberOfSpecificationsSupported++;
            break;
        case 0x05:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC-3");
            driveInfo->numberOfSpecificationsSupported++;
            break;
        case 0x06:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC-4");
            driveInfo->numberOfSpecificationsSupported++;
            break;
        case 0x07:
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "SPC-5");
            driveInfo->numberOfSpecificationsSupported++;
            break;
        default:
            break;
        }
        if (responseFormat == 1)
        {
            //response format of 1 means there is compliance with the Common Command Set specification, which is partial SCSI2 support.
            sprintf(driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported], "CCS");
            driveInfo->numberOfSpecificationsSupported++;
        }
        protectionSupported = inquiryData[5] & BIT0;
        if (version >= 4)
        {
            //Version Descriptors 1-8 (SPC2 and up)
            uint16_t versionDescriptor = 0;
            uint8_t versionIter = 0;
            for (; versionIter < 8; versionIter++)
            {
                versionDescriptor = 0;
                versionDescriptor = M_BytesTo2ByteValue(device->drive_info.scsiVpdData.inquiryData[(versionIter * 2) + 58], device->drive_info.scsiVpdData.inquiryData[(versionIter * 2) + 59]);
                if (versionDescriptor > 0)
                {
                    decypher_SCSI_Version_Descriptors(versionDescriptor, driveInfo->specificationsSupported[driveInfo->numberOfSpecificationsSupported]);
                    driveInfo->numberOfSpecificationsSupported++;
                }
            }
        }
        if (strcmp(driveInfo->vendorID, "SEAGATE ") == 0)
        {
            isSEAGATEVendorID = true;
        }
        if (isSEAGATEVendorID)
        {
            driveInfo->copyrightValid = true;
            memcpy(&driveInfo->copyrightInfo[0], &inquiryData[97], 48);
            driveInfo->copyrightInfo[49] = '\0';
        }
    }
    //VPD pages (read list of supported pages...if we don't get anything back, we'll dummy up a list of things we are interested in trying to read...this is to work around crappy USB bridges
    uint8_t *tempBuf = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * 2, sizeof(uint8_t));
    if (!tempBuf)
    {
        return MEMORY_FAILURE;
    }
    bool gotRotationRate = false;
    bool protectionType1Supported = false, protectionType2Supported = false, protectionType3Supported = false;
    if (version >= 2 || bridge_Does_Report_Unit_Serial_Number(device))//VPD pages indroduced in SCSI 2...also a USB hack
    {
        bool dummyUpVPDSupport = false;
        if (SUCCESS != scsi_Inquiry(device, tempBuf, 255, 0, true, false))
        {
            //for whatever reason, this device didn't return support for the list of supported pages, so set a flag telling us to dummy up a list so that we can still attempt to issue commands to pages we do need to try and get (this is a workaround for some really stupid USB bridges)
            dummyUpVPDSupport = true;
        }
        if (dummyUpVPDSupport == false)
        {
            uint8_t zeroedMem[255] = { 0 };
            if (memcmp(tempBuf, zeroedMem, 255) == 0)
            {
                //this case means that the command was successful, but we got nothing but zeros....which happens on some craptastic USB bridges
                dummyUpVPDSupport = true;
            }
        }
        if (dummyUpVPDSupport)
        {
            uint16_t offset = 4;
            //in here we will set up a fake supported VPD pages buffer so that we try to read the unit serial number page, the SAT page, and device identification page
            tempBuf[0] = peripheralQualifier << 5;
            tempBuf[0] |= peripheralDeviceType;
            //set page code
            tempBuf[1] = 0x00;
            //now each byte will reference a supported VPD page we want to dummy up. These should be in ascending order
            tempBuf[offset] = SUPPORTED_VPD_PAGES;//SCSI2
            ++offset;
            tempBuf[offset] = UNIT_SERIAL_NUMBER;//SCSI2
            ++offset;
            if (version >= 3)//SPC
            {
                tempBuf[offset] = DEVICE_IDENTIFICATION;
                ++offset;
            }
            tempBuf[offset] = ATA_INFORMATION;//SAT. Going to leave this in here no matter what other version info is available since SATLs needing this dummy data may support this regardless of other version info
            ++offset;
            if (version >= 6)//SBC3 - SPC4
            {
                if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                {
                    tempBuf[offset] = BLOCK_DEVICE_CHARACTERISTICS;
                    ++offset;
                }
            }
            //TODO: Add more pages to the dummy information as we need to. This may be useful to do in the future in case a device decides not to support a MANDATORY page or another page we care about
            //set page length (n-3)
            tempBuf[2] = M_Byte1(offset - 4);//msb
            tempBuf[3] = M_Byte0(offset - 4);//lsb
        }
        //first, get the length of the supported pages
        uint16_t supportedVPDPagesLength = M_BytesTo2ByteValue(tempBuf[2], tempBuf[3]);
        uint8_t *supportedVPDPages = (uint8_t*)calloc(supportedVPDPagesLength, sizeof(uint8_t));
        if (!supportedVPDPages)
        {
            perror("Error allocating memory for supported VPD pages!\n");
            return MEMORY_FAILURE;
        }
        memcpy(supportedVPDPages, &tempBuf[4], supportedVPDPagesLength);
        //now loop through and read pages as we need to, only reading the pages that we care about
        uint16_t vpdIter = 0;
        for (vpdIter = 0; vpdIter < supportedVPDPagesLength; vpdIter++)
        {
            switch (supportedVPDPages[vpdIter])
            {
            case UNIT_SERIAL_NUMBER:
            {
                uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                uint8_t *unitSerialNumber = (uint8_t*)calloc(unitSerialNumberPageLength, sizeof(uint8_t));
                if (!unitSerialNumber)
                {
                    perror("Error allocating memory to read the unit serial number");
                    continue;//continue the loop
                }
                if (SUCCESS == scsi_Inquiry(device, unitSerialNumber, unitSerialNumberPageLength, supportedVPDPages[vpdIter], true, false))
                {
                    uint16_t serialNumberLength = M_BytesTo2ByteValue(unitSerialNumber[2], unitSerialNumber[3]);
                    if (serialNumberLength > 0)
                    {
                        memcpy(driveInfo->serialNumber, &unitSerialNumber[4], M_Min(SERIAL_NUM_LEN + 1, serialNumberLength));
                        remove_Leading_And_Trailing_Whitespace(driveInfo->serialNumber);
                        driveInfo->serialNumber[M_Min(SERIAL_NUM_LEN, serialNumberLength)] = '\0';
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(unitSerialNumber);
                break;
            }
            case DEVICE_IDENTIFICATION:
            {
                uint8_t *deviceIdentification = (uint8_t*)calloc(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t));
                if (!deviceIdentification)
                {
                    perror("Error allocating memory to read device identification VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, deviceIdentification, INQ_RETURN_DATA_LENGTH, DEVICE_IDENTIFICATION, true, false))
                {
                    uint16_t devIDPageLen = M_BytesTo2ByteValue(deviceIdentification[2], deviceIdentification[3]);
                    if (devIDPageLen + 4 > INQ_RETURN_DATA_LENGTH)
                    {
                        //realloc and re-read the page with the larger pagelength
                        uint8_t *temp = (uint8_t*)realloc(deviceIdentification, devIDPageLen + 4);
                        if (!temp)
                        {
                            perror("realloc failure!\n");
                            return 101;
                        }
                        deviceIdentification = temp;
                        if (SUCCESS != scsi_Inquiry(device, deviceIdentification, devIDPageLen + 4, DEVICE_IDENTIFICATION, true, false))
                        {
                            //we had an error while trying to read the page...
                        }
                    }
                    //TODO: change this for parallel and PCIe?
                    driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0xFF;//set to something invalid
                    //Below we loop through to the designator descriptors to find the WWN, and on SAS set the active port.
                    //we get the active phy from the low byte of the WWN when we find the association field set to 01b
                    uint64_t accotiatedWWN = 0;
                    uint8_t association = 0;
                    uint8_t deviceIdentificationIter = 4;
                    uint16_t pageLength = M_BytesTo2ByteValue(deviceIdentification[2], deviceIdentification[3]);
                    uint8_t designatorLength = 0;
                    uint8_t protocolIdentifier = 0;
                    uint8_t designatorType = 0;
                    for (; deviceIdentificationIter < INQ_RETURN_DATA_LENGTH && deviceIdentificationIter < pageLength; deviceIdentificationIter += designatorLength)
                    {
                        association = (deviceIdentification[deviceIdentificationIter + 1] >> 4) & 0x03;
                        designatorLength = deviceIdentification[deviceIdentificationIter + 3] + 4;
                        protocolIdentifier = M_Nibble1(deviceIdentification[deviceIdentificationIter]);
                        designatorType = M_Nibble0(deviceIdentification[deviceIdentificationIter + 1]);
                        switch (association)
                        {
                        case 0://associated with the addressed logical unit
                            if (designatorType == 0x03)
                            {
                                driveInfo->worldWideNameSupported = true;
                                memcpy(&driveInfo->worldWideName, &deviceIdentification[deviceIdentificationIter + 4], 8);
                                byte_Swap_64(&driveInfo->worldWideName);
                                //check NAA to see if it's an extended WWN
                                uint8_t naa = M_Nibble15(driveInfo->worldWideName);
                                if (naa == 6)
                                {
                                    driveInfo->worldWideNameExtensionValid = true;
                                    memcpy(&driveInfo->worldWideName, &deviceIdentification[deviceIdentificationIter + 4 + 8], 8);
                                }
                            }
                            break;
                        case 1://associated with the target port that received the command
                            if (is_Seagate_Family(device))
                            {
                                if (protocolIdentifier == 0x06 && designatorType == 0x03)//SAS->only place that getting a port number makes sense right now since we aren't gathering port speed for other interfaces since it isn't reported.
                                {
                                    //we know we have found the right designator, so read the WWN, and check the lowest nibble for the port number
                                    memcpy(&accotiatedWWN, &deviceIdentification[deviceIdentificationIter + 4], 8);
                                    byte_Swap_64(&accotiatedWWN);
                                    uint8_t lowNibble = M_Nibble0(accotiatedWWN);
                                    lowNibble &= 0x3;
                                    if (lowNibble == 1)
                                    {
                                        driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0;
                                    }
                                    else if (lowNibble == 2)
                                    {
                                        driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 1;
                                    }
                                }
                            }
                            break;
                        case 2://associated with SCSI target device that contains the addressed logical unit
                            break;
                        case 3://reserved
                        default:
                            break;
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(deviceIdentification);
                break;
            }
            case EXTENDED_INQUIRY_DATA:
            {
                uint8_t *extendedInquiryData = (uint8_t*)calloc(VPD_EXTENDED_INQUIRY_LEN, sizeof(uint8_t));
                if (!extendedInquiryData)
                {
                    perror("Error allocating memory to read extended inquiry VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
                {
                    //get nvCache supported
                    driveInfo->nvCacheSupported = extendedInquiryData[6] & BIT1;
                    //get longDST time since we read this page!
                    driveInfo->longDSTTimeMinutes = M_BytesTo2ByteValue(extendedInquiryData[10], extendedInquiryData[11]);
                    //get supported protection types
                    switch (M_GETBITRANGE(extendedInquiryData[4], 5, 3))
                    {
                    case 0:
                        protectionType1Supported = true;
                        break;
                    case 1:
                        protectionType1Supported = true;
                        protectionType2Supported = true;
                        break;
                    case 2:
                        protectionType2Supported = true;
                        break;
                    case 3:
                        protectionType1Supported = true;
                        protectionType3Supported = true;
                        break;
                    case 4:
                        protectionType3Supported = true;
                        break;
                    case 5:
                        protectionType2Supported = true;
                        protectionType3Supported = true;
                        break;
                    case 6:
                        //read supported lengths and protection types VPD page
                    {
                        uint16_t supportedBlockSizesAndProtectionTypesLength = 4;//reallocate in a minute when we know how much to read
                        uint8_t *supportedBlockSizesAndProtectionTypes = (uint8_t*)calloc(supportedBlockSizesAndProtectionTypesLength, sizeof(uint8_t));
                        if (supportedBlockSizesAndProtectionTypes)
                        {
                            if (SUCCESS == scsi_Inquiry(device, supportedBlockSizesAndProtectionTypes, supportedBlockSizesAndProtectionTypesLength, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, true, false))
                            {
                                supportedBlockSizesAndProtectionTypesLength = M_BytesTo2ByteValue(supportedBlockSizesAndProtectionTypes[2], supportedBlockSizesAndProtectionTypes[3]);
                                uint8_t *temp = (uint8_t*)realloc(supportedBlockSizesAndProtectionTypes, supportedBlockSizesAndProtectionTypesLength * sizeof(uint8_t));
                                supportedBlockSizesAndProtectionTypes = temp;
                                if (SUCCESS == scsi_Inquiry(device, supportedBlockSizesAndProtectionTypes, supportedBlockSizesAndProtectionTypesLength, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, true, false))
                                {
                                    //loop through and find supported protection types...
                                    for (uint16_t offset = 4; offset < (supportedBlockSizesAndProtectionTypesLength + 4); offset += 8)
                                    {
                                        if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT1)
                                        {
                                            protectionType1Supported = true;
                                        }
                                        if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT2)
                                        {
                                            protectionType2Supported = true;
                                        }
                                        if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT3)
                                        {
                                            protectionType3Supported = true;
                                        }
                                        if (protectionType1Supported && protectionType2Supported && protectionType3Supported)
                                        {
                                            //all protection types supported so we can leave the loop
                                            break;
                                        }
                                    }
                                }
                            }
                            safe_Free(supportedBlockSizesAndProtectionTypes);
                        }
                        //no else...don't care that much right now...-TJE
                    }
                    break;
                    case 7:
                        protectionType1Supported = true;
                        protectionType2Supported = true;
                        protectionType3Supported = true;
                        break;
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(extendedInquiryData);
                break;
            }
            case BLOCK_DEVICE_CHARACTERISTICS:
            {
                uint8_t *blockDeviceCharacteristics = (uint8_t*)calloc(VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, sizeof(uint8_t));
                if (!blockDeviceCharacteristics)
                {
                    perror("Error allocating memory to read block device characteistics VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, blockDeviceCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, BLOCK_DEVICE_CHARACTERISTICS, true, false))
                {
                    driveInfo->rotationRate = M_BytesTo2ByteValue(blockDeviceCharacteristics[4], blockDeviceCharacteristics[5]);
                    gotRotationRate = true;
                    driveInfo->formFactor = M_Nibble0(blockDeviceCharacteristics[7]);
                    driveInfo->zonedDevice = (blockDeviceCharacteristics[8] & (BIT4 | BIT5)) >> 4;
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(blockDeviceCharacteristics);
                break;
            }
            case POWER_CONDITION:
                //reading this information has been moved to the mode pages below. - TJE
                //sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "EPC");
                //driveInfo->numberOfFeaturesSupported++;
                break;
            case POWER_CONSUMPTION:
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Power Comsumption");
                driveInfo->numberOfFeaturesSupported++;
                break;
            case LOGICAL_BLOCK_PROVISIONING:
            {
                uint8_t *logicalBlockProvisioning = (uint8_t*)calloc(VPD_LOGICAL_BLOCK_PROVISIONING_LEN, sizeof(uint8_t));
                if (!logicalBlockProvisioning)
                {
                    perror("Error allocating memory to read logical block provisioning VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, logicalBlockProvisioning, VPD_LOGICAL_BLOCK_PROVISIONING_LEN, LOGICAL_BLOCK_PROVISIONING, true, false))
                {
                    if (logicalBlockProvisioning[5] & BIT7)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "UNMAP");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(logicalBlockProvisioning);
                break;
            }
            case BLOCK_LIMITS:
            {
                uint8_t *blockLimits = (uint8_t*)calloc(VPD_BLOCK_LIMITS_LEN, sizeof(uint8_t));
                if (!blockLimits)
                {
                    perror("Error allocating memory to read logical block provisioning VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, blockLimits, VPD_BLOCK_LIMITS_LEN, BLOCK_LIMITS, true, false))
                {
                    uint64_t writeSameLength = M_BytesTo8ByteValue(blockLimits[36], blockLimits[37], blockLimits[38], blockLimits[39], blockLimits[40], blockLimits[41], blockLimits[42], blockLimits[43]);
                    if (writeSameLength > 0)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Write Same");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(blockLimits);
                break;
            }
            case ATA_INFORMATION:
            {
                uint8_t *ataInformation = (uint8_t*)calloc(VPD_ATA_INFORMATION_LEN, sizeof(uint8_t));
                if (!ataInformation)
                {
                    perror("Error allocating memory to read ATA Information VPD page");
                    continue;
                }
                if (SUCCESS == scsi_Inquiry(device, ataInformation, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
                {
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SAT");
                    memcpy(driveInfo->satVendorID, &ataInformation[8], 8);
                    memcpy(driveInfo->satProductID, &ataInformation[16], 16);
                    memcpy(driveInfo->satProductRevision, &ataInformation[32], 4);
                    driveInfo->numberOfFeaturesSupported++;
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                safe_Free(ataInformation);
                break;
            }
            default:
                break;
            }
        }
        safe_Free(supportedVPDPages);
    }
    else
    {
        //SCSI(1)/SASI/CCS don't have VPD pages. Try getting the SN from here (and that's all you get!)
        memcpy(driveInfo->serialNumber, &device->drive_info.scsiVpdData.inquiryData[36], SERIAL_NUM_LEN);
        device->drive_info.serialNumber[SERIAL_NUM_LEN] = '\0';
    }
    uint8_t protectionTypeEnabled = 0;//default to type 0
    //read capacity data - try read capacity 10 first, then do a read capacity 16. This is to work around some USB bridges passing the command and returning no data.
    uint8_t *readCapBuf = (uint8_t*)calloc(READ_CAPACITY_10_LEN, sizeof(uint8_t));
    if (!readCapBuf)
    {
        safe_Free(tempBuf);
        return MEMORY_FAILURE;
    }
    switch (peripheralDeviceType)
    {
    case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
    case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
    case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
    case PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE:
        if (SUCCESS == scsi_Read_Capacity_10(device, readCapBuf, READ_CAPACITY_10_LEN))
        {
            copy_Read_Capacity_Info(&driveInfo->logicalSectorSize, &driveInfo->physicalSectorSize, &driveInfo->maxLBA, &driveInfo->sectorAlignment, readCapBuf, false);
            if (version > 3)//SPC2 and higher can reference SBC2 and higher which introduced read capacity 16
            {
                //try a read capacity 16 anyways and see if the data from that was valid or not since that will give us a physical sector size whereas readcap10 data will not
                uint8_t* temp = (uint8_t*)realloc(readCapBuf, READ_CAPACITY_16_LEN * sizeof(uint8_t));
                if (!temp)
                {
                    safe_Free(tempBuf);
                    safe_Free(readCapBuf);
                    return MEMORY_FAILURE;
                }
                readCapBuf = temp;
                memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                {
                    uint32_t logicalBlockSize = 0;
                    uint32_t physicalBlockSize = 0;
                    uint64_t maxLBA = 0;
                    uint16_t sectorAlignment = 0;
                    copy_Read_Capacity_Info(&logicalBlockSize, &physicalBlockSize, &maxLBA, &sectorAlignment, readCapBuf, true);
                    //some USB drives will return success and no data, so check if this local var is 0 or not...if not, we can use this data
                    if (maxLBA != 0)
                    {
                        driveInfo->logicalSectorSize = logicalBlockSize;
                        driveInfo->physicalSectorSize = physicalBlockSize;
                        driveInfo->maxLBA = maxLBA;
                        driveInfo->sectorAlignment = sectorAlignment;
                    }
                    if (protectionSupported && readCapBuf[12] & BIT0)//protection enabled
                    {
                        switch (M_GETBITRANGE(readCapBuf[12], 3, 1))
                        {
                        case 0:
                            protectionTypeEnabled = 1;
                            break;
                        case 1:
                            protectionTypeEnabled = 2;
                            break;
                        case 2:
                            protectionTypeEnabled = 3;
                            break;
                        default:
                            break;
                        }
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
                //check for format corrupt
                uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
                {
                    driveInfo->isFormatCorrupt = true;
                }
            }
        }
        else
        {
            //check for format corrupt first
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
            {
                driveInfo->isFormatCorrupt = true;
            }

            //try read capacity 16, if that fails we are done trying
            uint8_t* temp = (uint8_t*)realloc(readCapBuf, READ_CAPACITY_16_LEN * sizeof(uint8_t));
            if (temp == NULL)
            {
                safe_Free(tempBuf);
                safe_Free(readCapBuf);
                return MEMORY_FAILURE;
            }
            readCapBuf = temp;
            memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
            if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
            {
                copy_Read_Capacity_Info(&driveInfo->logicalSectorSize, &driveInfo->physicalSectorSize, &driveInfo->maxLBA, &driveInfo->sectorAlignment, readCapBuf, true);
                if (protectionSupported && readCapBuf[12] & BIT0)//protection enabled
                {
                    switch (M_GETBITRANGE(readCapBuf[12], 3, 1))
                    {
                    case 0:
                        protectionTypeEnabled = 1;
                        break;
                    case 1:
                        protectionTypeEnabled = 2;
                        break;
                    case 2:
                        protectionTypeEnabled = 3;
                        break;
                    default:
                        break;
                    }
                }
            }
            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
            //check for format corrupt first
            senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
            {
                driveInfo->isFormatCorrupt = true;
            }
        }
        break;
    default:
        break;
    }
    safe_Free(readCapBuf);
    if (protectionSupported)
    {
        //set protection types supported up here.
        if (protectionType1Supported)
        {
            if (protectionTypeEnabled == 1)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 1 [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 1");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (protectionType2Supported)
        {
            if (protectionTypeEnabled == 2)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 2 [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 2");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
        if (protectionType3Supported)
        {
            if (protectionTypeEnabled == 3)
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 3 [Enabled]");
            }
            else
            {
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Protection Type 3");
            }
            driveInfo->numberOfFeaturesSupported++;
        }
    }
    if (version >= 6 && SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, 0, NULL))//security protocol commands introduced in SPC4. TODO: may need to drop to SPC3 for some devices. Need to investigate
    {
        //Check for TCG support - try sending a security protocol in command to get the list of security protocols (check for security protocol EFh? We can do that for ATA Security information)
        memset(tempBuf, 0, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, 512, tempBuf))
        {
            bool tcgFeatureFound = false;
            uint16_t length = M_BytesTo2ByteValue(tempBuf[6], tempBuf[7]);
            uint16_t bufIter = 8;
            for (; (bufIter - 8) < length && (bufIter - 8) < 512; bufIter++)
            {
                switch (tempBuf[bufIter])
                {
                case SECURITY_PROTOCOL_INFORMATION:
                    break;
                case SECURITY_PROTOCOL_TCG_1:
                case SECURITY_PROTOCOL_TCG_2:
                case SECURITY_PROTOCOL_TCG_3:
                case SECURITY_PROTOCOL_TCG_4:
                case SECURITY_PROTOCOL_TCG_5:
                case SECURITY_PROTOCOL_TCG_6:
                    driveInfo->encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                    if (!tcgFeatureFound)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "TCG");
                        driveInfo->numberOfFeaturesSupported++;
                        tcgFeatureFound = true;
                    }
                    break;
                case SECURITY_PROTOCOL_CbCS:
                case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
                case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
                case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
                case SECURITY_PROTOCOL_IKE_V2_SCSI:
                case SECURITY_PROTOCOL_NVM_EXPRESS:
                    break;
                case SECURITY_PROTOCOL_SCSA:
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "SCSA");
                    driveInfo->numberOfFeaturesSupported++;
                    break;
                case SECURITY_PROTOCOL_JEDEC_UFS:
                case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
                    break;
                case SECURITY_PROTOCOL_IEEE_1667:
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "IEEE 1667");
                    driveInfo->numberOfFeaturesSupported++;
                    break;
                case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
                {
                    //read the data from this page to set ATA security information
                    uint8_t ataSecurityInfo[16] = { 0 };
                    if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0, false, 16, ataSecurityInfo))
                    {
                        driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[2], ataSecurityInfo[3]);
                        driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[4], ataSecurityInfo[5]);
                        driveInfo->ataSecurityInformation.masterPasswordIdentifier = M_BytesTo2ByteValue(ataSecurityInfo[6], ataSecurityInfo[7]);
                        //check the bits now
                        if (ataSecurityInfo[8] & BIT0)
                        {
                            driveInfo->ataSecurityInformation.masterPasswordCapability = true;
                        }
                        if (ataSecurityInfo[9] & BIT5)
                        {
                            driveInfo->ataSecurityInformation.enhancedEraseSupported = true;
                        }
                        if (ataSecurityInfo[9] & BIT4)
                        {
                            driveInfo->ataSecurityInformation.securityCountExpired = true;
                        }
                        if (ataSecurityInfo[9] & BIT3)
                        {
                            driveInfo->ataSecurityInformation.securityFrozen = true;
                        }
                        if (ataSecurityInfo[9] & BIT2)
                        {
                            driveInfo->ataSecurityInformation.securityLocked = true;
                        }
                        if (ataSecurityInfo[9] & BIT1)
                        {
                            driveInfo->ataSecurityInformation.securityEnabled = true;
                        }
                        if (ataSecurityInfo[9] & BIT0)
                        {
                            driveInfo->ataSecurityInformation.securitySupported = true;
                        }
                    }
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "ATA Security");
                    driveInfo->numberOfFeaturesSupported++;
                }
                break;
                default:
                    break;
                }
            }
        }
        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
        {
            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
            scsi_Test_Unit_Ready(device, NULL);
        }
    }
    driveInfo->percentEnduranceUsed = -1;//set to this to filter out later

    bool smartStatusRead = false;
    if (version >= 2 && peripheralDeviceType != PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE)//SCSI2 introduced log pages
    {
        bool dummyUpLogPages = false;
        bool subpagesSupported = true;
        //Check log pages for data->start with list of pages and subpages
        memset(tempBuf, 0, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, tempBuf, LEGACY_DRIVE_SEC_SIZE))
        {
            if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
            {
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
            }
            //either device doesn't support logs, or it just doesn't support subpages, so let's try reading the list of supported pages (no subpages) before saying we need to dummy up the list
            if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, tempBuf, LEGACY_DRIVE_SEC_SIZE))
            {
                dummyUpLogPages = true;
                if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
            else
            {
                subpagesSupported = false;
            }
        }
        if (!dummyUpLogPages)
        {
            //memcmp to make sure we weren't given zeros
            uint8_t zeroMem[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (memcmp(zeroMem, tempBuf, LEGACY_DRIVE_SEC_SIZE) == 0)
            {
                dummyUpLogPages = true;
            }
        }
        //this is really a work-around for USB drives since some DO support pages, but the don't actually list them (same as the VPD pages above). Most USB drives don't work though - TJE
        if (dummyUpLogPages)
        {
            uint16_t offset = 4;
            uint8_t increment = 1;//change to 2 for subpages (spc4 added subpages)
            if (version >= 6)
            {
                subpagesSupported = true;
                increment = 2;
            }
            memset(tempBuf, 0, LEGACY_DRIVE_SEC_SIZE);
            tempBuf[0] = 0;
            tempBuf[1] = 0;

            //descriptors Need to be added based on support for subpages or not!
            tempBuf[offset] = LP_SUPPORTED_LOG_PAGES;//just to be correct/accurate
            if (subpagesSupported)
            {
                tempBuf[offset + 1] = 0;//subpage
                offset += increment;
                tempBuf[offset] = LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES;//just to be correct/accurate
                tempBuf[offset + 1] = 0xFF;//supported subpages
            }
            offset += increment;
            tempBuf[offset] = LP_WRITE_ERROR_COUNTERS;//not likely available on USB
            offset += increment;
            tempBuf[offset] = LP_READ_ERROR_COUNTERS;//not likely available on USB
            offset += increment;

            //if SBC3! (sequential access device page, so also check peripheral device type)
            if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
            {
                if (version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                {
                    tempBuf[offset] = LP_LOGICAL_BLOCK_PROVISIONING;//SBC3
                    offset += increment;
                }
            }

            if (version >= 4)//SPC2
            {
                tempBuf[offset] = LP_TEMPERATURE;//not likely available on USB
                offset += increment;
            }

            if (subpagesSupported && version >= 7)
            {
                tempBuf[offset] = LP_ENVIRONMENTAL_REPORTING;//not likely available on USB
                tempBuf[offset + 1] = 0x01;//subpage (page number is same as temperature)
                offset += increment;
            }
            if (version >= 4)//SPC2
            {
                tempBuf[offset] = LP_START_STOP_CYCLE_COUNTER;//just to be correct, we're not reading this today
                offset += increment;
            }
            if (version >= 7)//SBC4?
            {
                tempBuf[offset] = LP_UTILIZATION;//not likely available on USB
                tempBuf[offset + 1] = 0x01;//subpage
                offset += increment;
            }
            if (version >= 4)//SPC2
            {
                tempBuf[offset] = LP_APPLICATION_CLIENT;
                offset += increment;
                tempBuf[offset] = LP_SELF_TEST_RESULTS;
                offset += increment;
            }

            if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
            {
                if (version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                {
                    tempBuf[offset] = LP_SOLID_STATE_MEDIA;//not likely available on USB
                    offset += increment;
                }
            }

            if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
            {
                if (version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                {
                    tempBuf[offset] = LP_BACKGROUND_SCAN_RESULTS;//not likely available on USB
                    offset += increment;
                }
            }

            if (version >= 6)
            {
                tempBuf[offset] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;//not likely available on USB
                offset += increment;
            }

            if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
            {
                if (version >= 5) //SBC2 is to be used in conjunction with SPC3. We may need to drop this one level later, but this should be ok
                {
                    tempBuf[offset] = LP_INFORMATION_EXCEPTIONS;
                    offset += increment;
                }
            }

            //page length
            tempBuf[2] = M_Byte1(offset - 4);
            tempBuf[3] = M_Byte0(offset - 4);
        }
        //loop through log pages and read them:
        uint16_t logPageIter = LOG_PAGE_HEADER_LENGTH;//log page descriptors start on offset 4 and are 2 bytes long each
        uint16_t supportedPagesLength = M_BytesTo2ByteValue(tempBuf[2], tempBuf[3]);
        uint8_t incrementAmount = subpagesSupported ? 2 : 1;
        for (; logPageIter < M_Min(supportedPagesLength + LOG_PAGE_HEADER_LENGTH, LEGACY_DRIVE_SEC_SIZE); logPageIter += incrementAmount)
        {
            uint8_t pageCode = tempBuf[logPageIter] & 0x3F;//outer switch statement
            uint8_t subpageCode = 0;
            if (subpagesSupported)
            {
                subpageCode = tempBuf[logPageIter + 1];//inner switch statement
            }
            switch (pageCode)
            {
            case LP_WRITE_ERROR_COUNTERS:
                if (subpageCode == 0)
                {
                    //we need parameter code 5h (total bytes processed)
                    //assume we only need to read 16 bytes to get this value
                    uint8_t *writeErrorData = (uint8_t*)calloc(16 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!writeErrorData)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0005, writeErrorData, 16))
                    {
                        //check the length before we start trying to read the number of bytes in.
                        if (M_BytesTo2ByteValue(writeErrorData[4], writeErrorData[5]) == 0x0005)
                        {
                            uint8_t paramLength = writeErrorData[7];
                            switch (paramLength)
                            {
                            case 1://single byte
                                driveInfo->totalBytesWritten = writeErrorData[8];
                                break;
                            case 2://word
                                driveInfo->totalBytesWritten = M_BytesTo2ByteValue(writeErrorData[8], writeErrorData[9]);
                                break;
                            case 4://double word
                                driveInfo->totalBytesWritten = M_BytesTo4ByteValue(writeErrorData[8], writeErrorData[9], writeErrorData[10], writeErrorData[11]);
                                break;
                            case 8://quad word
                                driveInfo->totalBytesWritten = M_BytesTo8ByteValue(writeErrorData[8], writeErrorData[9], writeErrorData[10], writeErrorData[11], writeErrorData[12], writeErrorData[13], writeErrorData[14], writeErrorData[15]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                break;
                            }
                            //now convert this to LBAs based on the logical sector size
                            if (driveInfo->logicalSectorSize)
                            {
                                driveInfo->totalLBAsWritten = driveInfo->totalBytesWritten / driveInfo->logicalSectorSize;
                            }
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(writeErrorData);
                }
                break;
            case LP_READ_ERROR_COUNTERS:
                if (subpageCode == 0)
                {
                    //we need parameter code 5h (total bytes processed)
                    //assume we only need to read 16 bytes to get this value
                    uint8_t *readErrorData = (uint8_t*)calloc(16 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!readErrorData)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0005, readErrorData, 16))
                    {
                        if (M_BytesTo2ByteValue(readErrorData[4], readErrorData[5]) == 0x0005)
                        {
                            //check the length before we start trying to read the number of bytes in.
                            uint8_t paramLength = readErrorData[7];
                            switch (paramLength)
                            {
                            case 1://single byte
                                driveInfo->totalBytesRead = readErrorData[8];
                                break;
                            case 2://word
                                driveInfo->totalBytesRead = M_BytesTo2ByteValue(readErrorData[8], readErrorData[9]);
                                break;
                            case 4://double word
                                driveInfo->totalBytesRead = M_BytesTo4ByteValue(readErrorData[8], readErrorData[9], readErrorData[10], readErrorData[11]);
                                break;
                            case 8://quad word
                                driveInfo->totalBytesRead = M_BytesTo8ByteValue(readErrorData[8], readErrorData[9], readErrorData[10], readErrorData[11], readErrorData[12], readErrorData[13], readErrorData[14], readErrorData[15]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                break;
                            }
                            //now convert this to LBAs based on the logical sector size
                            if (driveInfo->logicalSectorSize)
                            {
                                driveInfo->totalLBAsRead = driveInfo->totalBytesRead / driveInfo->logicalSectorSize;
                            }
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(readErrorData);
                }
                break;
            case LP_LOGICAL_BLOCK_PROVISIONING:
                /*if (subpageCode == 0)
                {

                }*/
                break;
            case LP_TEMPERATURE://also environmental reporting
                switch (subpageCode)
                {
                case 0://temperature
                {
                    uint8_t *temperatureData = (uint8_t*)calloc(10 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!temperatureData)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, temperatureData, 10))
                    {
                        driveInfo->temperatureData.temperatureDataValid = true;
                        driveInfo->temperatureData.currentTemperature = temperatureData[9];
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(temperatureData);
                }
                break;
                case 1://environmental reporting
                {
                    uint8_t *environmentReporting = (uint8_t*)calloc(16 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!environmentReporting)
                    {
                        break;
                    }
                    //get temperature data first
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, environmentReporting, 16))
                    {
                        driveInfo->temperatureData.temperatureDataValid = true;
                        driveInfo->temperatureData.currentTemperature = (int8_t)environmentReporting[9];
                        driveInfo->temperatureData.highestTemperature = (int8_t)environmentReporting[10];
                        driveInfo->temperatureData.lowestTemperature = (int8_t)environmentReporting[11];
                        driveInfo->temperatureData.highestValid = true;
                        driveInfo->temperatureData.lowestValid = true;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //now get humidity data if available
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0100, environmentReporting, 16))
                    {
                        driveInfo->humidityData.humidityDataValid = true;
                        driveInfo->humidityData.currentHumidity = environmentReporting[9];
                        driveInfo->humidityData.highestHumidity = environmentReporting[10];
                        driveInfo->humidityData.lowestHumidity = environmentReporting[11];
                        driveInfo->humidityData.highestValid = true;
                        driveInfo->humidityData.lowestValid = true;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(environmentReporting);
                }
                break;
                default:
                    break;
                }
                break;
            case LP_UTILIZATION://also start-stop cycle counter
                switch (subpageCode)
                {
                case 0x01://utilization
                {
                    uint8_t *utilizationData = (uint8_t*)calloc(10 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!utilizationData)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, utilizationData, 10))
                    {
                        //bytes 9 & 10
                        driveInfo->deviceReportedUtilizationRate = ((double)M_BytesTo2ByteValue(utilizationData[8], utilizationData[9])) / 1000.0;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(utilizationData);
                }
                break;
                default:
                    break;
                }
                break;
            case LP_APPLICATION_CLIENT:
                switch (subpageCode)
                {
                case 0x00://application client
                {
                    uint8_t *applicationClient = (uint8_t*)calloc(4 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!applicationClient)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, applicationClient, 4))
                    {
                        //add "Application Client Logging" to supported features :)
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Application Client Logging");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(applicationClient);
                }
                break;
                default:
                    break;
                }
                break;
            case LP_SELF_TEST_RESULTS:
                if (subpageCode == 0)
                {
                    uint8_t *selfTestResults = (uint8_t*)calloc(LP_SELF_TEST_RESULTS_LEN * sizeof(uint8_t), sizeof(uint8_t));
                    if (!selfTestResults)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, selfTestResults, LP_SELF_TEST_RESULTS_LEN))
                    {
                        uint8_t parameterOffset = 4;
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Self Test");
                        driveInfo->numberOfFeaturesSupported++;
                        //get the last DST information (parameter code 1)
                        driveInfo->dstInfo.informationValid = true;
                        driveInfo->dstInfo.resultOrStatus = M_Nibble0(selfTestResults[parameterOffset + 4]);
                        driveInfo->dstInfo.testNumber = M_Nibble1(selfTestResults[parameterOffset + 4]) >> 1;
                        driveInfo->dstInfo.powerOnHours = M_BytesTo2ByteValue(selfTestResults[parameterOffset + 6], selfTestResults[parameterOffset + 7]);
                        driveInfo->dstInfo.errorLBA = M_BytesTo8ByteValue(selfTestResults[parameterOffset + 8], selfTestResults[parameterOffset + 9], selfTestResults[parameterOffset + 10], selfTestResults[parameterOffset + 11], selfTestResults[parameterOffset + 12], selfTestResults[parameterOffset + 13], selfTestResults[parameterOffset + 14], selfTestResults[parameterOffset + 15]);
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(selfTestResults);
                }
                break;
            case LP_SOLID_STATE_MEDIA:
                if (subpageCode == 0)
                {
                    //need parameter 0001h
                    uint8_t *ssdEnduranceData = (uint8_t*)calloc(12 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!ssdEnduranceData)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, ssdEnduranceData, 12))
                    {
                        //bytes 7 of parameter 1 (or byte 12)
                        driveInfo->percentEnduranceUsed = (double)ssdEnduranceData[11];
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(ssdEnduranceData);
                }
                break;
            case LP_BACKGROUND_SCAN_RESULTS:
                if (subpageCode == 0)
                {
                    //reading power on minutes from here
                    uint8_t *backgroundScanResults = (uint8_t*)calloc(19 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!backgroundScanResults)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, backgroundScanResults, 19))
                    {
                        //bytes 8 to 11
                        driveInfo->powerOnMinutes = M_BytesTo4ByteValue(backgroundScanResults[8], backgroundScanResults[9], backgroundScanResults[10], backgroundScanResults[11]);
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(backgroundScanResults);
                }
                break;
            case LP_GENERAL_STATISTICS_AND_PERFORMANCE:
                if (subpageCode == 0)
                {
                    //parameter code 1 is what we're interested in for this one
                    uint8_t *generalStatsAndPerformance = (uint8_t*)calloc(72 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!generalStatsAndPerformance)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, generalStatsAndPerformance, 72))
                    {
                        //total lbas written (number of logical blocks received)
                        driveInfo->totalLBAsWritten = M_BytesTo8ByteValue(generalStatsAndPerformance[24], generalStatsAndPerformance[25], generalStatsAndPerformance[26], generalStatsAndPerformance[27], generalStatsAndPerformance[28], generalStatsAndPerformance[29], generalStatsAndPerformance[30], generalStatsAndPerformance[31]);
                        //convert to bytes written
                        driveInfo->totalBytesWritten = driveInfo->totalLBAsWritten * driveInfo->logicalSectorSize;
                        //total lbas read (number of logical blocks transmitted)
                        driveInfo->totalLBAsRead = M_BytesTo8ByteValue(generalStatsAndPerformance[32], generalStatsAndPerformance[33], generalStatsAndPerformance[34], generalStatsAndPerformance[35], generalStatsAndPerformance[36], generalStatsAndPerformance[37], generalStatsAndPerformance[38], generalStatsAndPerformance[39]);
                        //convert to bytes written
                        driveInfo->totalBytesRead = driveInfo->totalLBAsRead * driveInfo->logicalSectorSize;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(generalStatsAndPerformance);
                }
                break;
            case LP_INFORMATION_EXCEPTIONS:
                if (subpageCode == 0)
                {
                    uint8_t *informationExceptions = (uint8_t*)calloc(11 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!informationExceptions)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, informationExceptions, 11))
                    {
                        smartStatusRead = true;
                        if (informationExceptions[8] == 0)//if the ASC is 0, then no trip
                        {
                            driveInfo->smartStatus = 0;
                        }
                        else//we have a trip condition...don't care what the specific trip is though
                        {
                            driveInfo->smartStatus = 1;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                        driveInfo->smartStatus = 2;
                    }
                    else
                    {
                        driveInfo->smartStatus = 2;
                    }
                    safe_Free(informationExceptions);
                }
                break;
            case 0x3C://Vendor specific page. we're checking this page on Seagate drives for an enhanced usage indicator on SSDs (PPM value)
                if (is_Seagate_Family(device) == SEAGATE || is_Seagate_Family(device) == SEAGATE_VENDOR_A)
                {
                    uint8_t *ssdUsage = (uint8_t*)calloc(12 * sizeof(uint8_t), sizeof(uint8_t));
                    if (!ssdUsage)
                    {
                        break;
                    }
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, 0, 0x8004, ssdUsage, 12))
                    {
                        driveInfo->percentEnduranceUsed = (((double)M_BytesTo4ByteValue(ssdUsage[8], ssdUsage[9], ssdUsage[10], ssdUsage[11])) / 1000000.00) * 100.00;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(ssdUsage);
                }
                break;
            default:
                break;
            }
        }
    }
    if (!smartStatusRead)
    {
        //we didn't read the informational exceptions log page, so we need to set this to SMART status unknown
        driveInfo->smartStatus = 2;
    }

    if (version >= 2 || responseFormat == 1)
    {
        uint16_t numberOfPages = 0;
        uint16_t offset = 0;
        //create a list of mode pages (and any subpages) we care about reading and go through that list reading each one
        uint8_t listOfModePagesAndSubpages[512] = { 0 };//allow 10 entries in the list...update the loop condition below if this is adjusted
        //format for page list is first byte = page, 2nd byte = subpage, then increment and look at the next page
        listOfModePagesAndSubpages[offset] = MP_READ_WRITE_ERROR_RECOVERY;//AWRE, ARRE
        offset += 2;
        if (!gotRotationRate && (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE))
        {
            listOfModePagesAndSubpages[offset] = MP_RIGID_DISK_GEOMETRY;//To get medium rotation rate if we didn't already get it. This page is long obsolete
            offset += 2;
        }
        listOfModePagesAndSubpages[offset] = MP_CACHING;//WCE, DRA, NV_DIS?
        offset += 2;
        if (version >= 4)//control mode page didn't get long DST info until SPC2
        {
            listOfModePagesAndSubpages[offset] = MP_CONTROL;//Long DST Time
            offset += 2;
        }
        if (version >= 5)//SPC3 added subpage codes
        {
            listOfModePagesAndSubpages[offset] = MP_CONTROL;//DLC
            listOfModePagesAndSubpages[offset + 1] = 0x01;
            offset += 2;
            //IO Advice hints is in SBC4
            listOfModePagesAndSubpages[offset] = MP_CONTROL;//IO Advice Hints (can we read this page or not basically)
            listOfModePagesAndSubpages[offset + 1] = 0x05;
            offset += 2;
            //From SAT spec
            listOfModePagesAndSubpages[offset] = MP_CONTROL;//PATA control (can PATA transfer speeds be changed)
            listOfModePagesAndSubpages[offset + 1] = 0xF1;
            offset += 2;
        }
        if (version >= 4)//SPC2 added this page
        {
            listOfModePagesAndSubpages[offset] = MP_PROTOCOL_SPECIFIC_PORT;//get interface type
            listOfModePagesAndSubpages[offset + 1] = 0;
            offset += 2;
        }
        if (version >= 5)//SPC3 added subpage codes
        {
            listOfModePagesAndSubpages[offset] = MP_PROTOCOL_SPECIFIC_PORT;//get SAS phy speed
            listOfModePagesAndSubpages[offset + 1] = 1;
            offset += 2;
        }
        if (version >= 3)//SPC added this page
        {
            listOfModePagesAndSubpages[offset] = MP_POWER_CONDTION;//EPC and older standby/idle timers
            listOfModePagesAndSubpages[offset + 1] = 0;
            offset += 2;
        }
        if (version >= 5)//SPC3 added subpage codes
        {
            //ATA Advanced Power Management page from SAT2
            listOfModePagesAndSubpages[offset] = MP_POWER_CONDTION;//ATA APM
            listOfModePagesAndSubpages[offset + 1] = 0xF1;//reading this for the ATA APM settings (check if supported really)
            offset += 2;
        }
        if (version >= 3)//Added in SPC
        {
            listOfModePagesAndSubpages[offset] = MP_INFORMATION_EXCEPTIONS_CONTROL;//SMART/informational exceptions & MRIE value. Dexcept? Warnings?
            listOfModePagesAndSubpages[offset + 1] = 0;
            offset += 2;
        }
        if (version >= 5)//SPC3 added subpage codes
        {
            listOfModePagesAndSubpages[offset] = MP_BACKGROUND_CONTROL;//EN_BMS, EN_PS
            listOfModePagesAndSubpages[offset + 1] = 0x01;
            offset += 2;
        }
        numberOfPages = offset / 2;
        uint16_t modeIter = 0;
        uint8_t protocolIdentifier = 0;
        for (uint16_t pageCounter = 0; modeIter < offset && pageCounter < numberOfPages; modeIter += 2, ++pageCounter)
        {
            uint8_t pageCode = listOfModePagesAndSubpages[modeIter];
            uint8_t subPageCode = listOfModePagesAndSubpages[modeIter + 1];
            switch (pageCode)
            {
            case MP_READ_WRITE_ERROR_RECOVERY:
                switch (subPageCode)
                {
                case 0:
                    //check if AWRE and ARRE are supported or can be changed before checking if they are enabled or not.
                {
                    char *awreString = NULL;
                    char *arreString = NULL;
                    uint8_t readWriteErrorRecovery[12 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false, defaultsRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 12 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_DEFAULT_VALUES, readWriteErrorRecovery))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 12 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_DEFAULT_VALUES, readWriteErrorRecovery))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (defaultsRead)
                    {
                        //awre
                        if (readWriteErrorRecovery[headerLength + 2] & BIT7)
                        {
                            if (!awreString)
                            {
                                awreString = (char*)calloc(30, sizeof(char));
                            }
                            else
                            {
                                awreString = (char*)realloc(awreString, 30 * sizeof(char));
                                memset(awreString, 0, 30);
                            }
                            strcpy(awreString, "Automatic Write Reassignment");
                        }
                        //arre
                        if (readWriteErrorRecovery[headerLength + 2] & BIT6)
                        {
                            if (!arreString)
                            {
                                arreString = (char*)calloc(30, sizeof(char));
                            }
                            else
                            {
                                arreString = (char*)realloc(arreString, 30 * sizeof(char));
                                memset(arreString, 0, 30);
                            }
                            strcpy(arreString, "Automatic Read Reassignment");
                        }
                    }
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 12 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, readWriteErrorRecovery))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (readWriteErrorRecovery[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 12 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, readWriteErrorRecovery))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (readWriteErrorRecovery[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    if (pageRead)
                    {
                        //awre
                        if (readWriteErrorRecovery[headerLength + 2] & BIT7)
                        {
                            if (!awreString)
                            {
                                awreString = (char*)calloc(40, sizeof(char));
                            }
                            else
                            {
                                awreString = (char*)realloc(awreString, 40 * sizeof(char));
                                memset(awreString, 0, 40);
                            }
                            strcpy(awreString, "Automatic Write Reassignment [Enabled]");
                        }
                        //arre
                        if (readWriteErrorRecovery[headerLength + 2] & BIT6)
                        {
                            if (!arreString)
                            {
                                arreString = (char*)calloc(40, sizeof(char));
                            }
                            else
                            {
                                arreString = (char*)realloc(arreString, 40 * sizeof(char));
                                memset(arreString, 0, 40);
                            }
                            strcpy(arreString, "Automatic Read Reassignment [Enabled]");
                        }
                    }
                    if (awreString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", awreString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    if (arreString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", arreString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    safe_Free(awreString);
                    safe_Free(arreString);
                }
                break;
                default:
                    break;
                }
                break;
            case MP_RIGID_DISK_GEOMETRY:
                switch (subPageCode)
                {
                case 0:
                    if (!gotRotationRate)
                    {
                        uint8_t rigidGeometry[24 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                        bool pageRead = false;
                        uint8_t headerLength = 0;
                        if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 24 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, rigidGeometry))
                        {
                            pageRead = true;
                            headerLength = MODE_PARAMETER_HEADER_10_LEN;
                            if (rigidGeometry[3] & BIT7)
                            {
                                driveInfo->isWriteProtected = true;
                            }
                        }
                        else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 24 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, rigidGeometry))
                        {
                            pageRead = true;
                            headerLength = MODE_PARAMETER_HEADER_6_LEN;
                            if (rigidGeometry[2] & BIT7)
                            {
                                driveInfo->isWriteProtected = true;
                            }

                        }
                        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                        {
                            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                            scsi_Test_Unit_Ready(device, NULL);
                        }
                        if (pageRead)
                        {
                            driveInfo->rotationRate = M_BytesTo2ByteValue(rigidGeometry[headerLength + 20], rigidGeometry[headerLength + 21]);
                        }
                    }
                    break;
                default:
                    break;
                }
                break;
            case MP_CACHING:
                switch (subPageCode)
                {
                case 0:
                {
                    uint8_t cachingPage[20 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 20 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, cachingPage))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (cachingPage[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 20 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, cachingPage))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (cachingPage[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (pageRead)
                    {
                        //NV_DIS
                        driveInfo->nvCacheEnabled = cachingPage[headerLength + 13] & BIT0 ? false : true;//bit being set means disabled the cache, being set to 0 means cache is enabled.

                        //WCE
                        driveInfo->writeCacheEnabled = cachingPage[headerLength + 2] & BIT2 ? true : false;
                        if (driveInfo->writeCacheEnabled)
                        {
                            driveInfo->writeCacheSupported = true;
                        }
                        //DRA
                        driveInfo->readLookAheadEnabled = cachingPage[headerLength + 12] & BIT5 ? false : true;
                        if (driveInfo->readLookAheadEnabled)
                        {
                            driveInfo->readLookAheadSupported = true;
                        }
                        //check for supported if it's not already set
                        if (!driveInfo->writeCacheSupported || !driveInfo->readLookAheadSupported)
                        {
                            //we didn't get is supported from above, so check the changable page
                            memset(cachingPage, 0, 20 + MODE_PARAMETER_HEADER_10_LEN);
                            pageRead = false;//reset to false before reading the changable values page
                            if (version >= 2 && headerLength == MODE_PARAMETER_HEADER_10_LEN && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 20 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CHANGABLE_VALUES, cachingPage))
                            {
                                pageRead = true;
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                            }
                            else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 20 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CHANGABLE_VALUES, cachingPage))
                            {
                                pageRead = true;
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                            }
                            else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                            {
                                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                                scsi_Test_Unit_Ready(device, NULL);
                            }
                            if (pageRead)
                            {
                                driveInfo->writeCacheSupported = cachingPage[headerLength + 2] & BIT2 ? true : false;
                                driveInfo->readLookAheadSupported = cachingPage[headerLength + 12] & BIT5 ? false : true;
                            }
                        }
                    }
                }
                break;
                default:
                    break;
                }
                break;
            case MP_CONTROL:
                switch (subPageCode)
                {
                case 0://control mode page. No subpage
                {
                    uint8_t controlPage[MP_CONTROL_LEN + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, MP_CONTROL_LEN + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, controlPage))
                    {
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        pageRead = true;
                        if (controlPage[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, MP_CONTROL_LEN + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, controlPage))
                    {
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        pageRead = true;
                        if (controlPage[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (pageRead)
                    {
                        //check the page code and page length
                        if (M_GETBITRANGE(controlPage[headerLength + 0], 5, 0) == MP_CONTROL)
                        {
                            //check length since the page needs to be long enough for this data. Earlier specs this page was shorter
                            if (controlPage[headerLength + 1] == 0x0A)
                            {
                                if (driveInfo->longDSTTimeMinutes == 0)//checking for zero since we may have already gotten this from the Extended Inquiry VPD page
                                {
                                    driveInfo->longDSTTimeMinutes = ((M_BytesTo2ByteValue(controlPage[headerLength + 10], controlPage[headerLength + 11]) + 60) - 1) / 60;//rounding up to nearest minute
                                }
                            }
                        }
                    }
                }
                break;
                case 1://controlExtension
                {
                    //check if DLC is supported or can be changed before checking if they are enabled or not.
                    char *dlcString = NULL;
                    uint8_t controlExtensionPage[MP_CONTROL_EXTENSION_LEN + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false, defaultsRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, MP_CONTROL_EXTENSION_LEN + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_DEFAULT_VALUES, controlExtensionPage))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, MP_CONTROL_EXTENSION_LEN + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_DEFAULT_VALUES, controlExtensionPage))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (controlExtensionPage[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (controlExtensionPage[headerLength + 1] != 0x01)
                        {
                            defaultsRead = false;
                        }
                    }
                    else
                    {
                        defaultsRead = false;
                    }
                    if (defaultsRead)
                    {
                        //dlc
                        if (controlExtensionPage[headerLength + 4] & BIT3)
                        {
                            if (!dlcString)
                            {
                                dlcString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                dlcString = (char*)realloc(dlcString, 50 * sizeof(char));
                                memset(dlcString, 0, 50);
                            }
                            strcpy(dlcString, "Device Life Control");
                        }
                    }
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, MP_CONTROL_EXTENSION_LEN + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, controlExtensionPage))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (controlExtensionPage[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, MP_CONTROL_EXTENSION_LEN + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, controlExtensionPage))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (controlExtensionPage[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (controlExtensionPage[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (controlExtensionPage[headerLength + 1] != 0x01)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        //dlc
                        if (controlExtensionPage[headerLength + 4] & BIT3)
                        {
                            if (!dlcString)
                            {
                                dlcString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                dlcString = (char*)realloc(dlcString, 50 * sizeof(char));
                                memset(dlcString, 0, 50);
                            }
                            strcpy(dlcString, "Device Life Control [Enabled]");
                        }
                    }
                    if (dlcString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", dlcString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    safe_Free(dlcString);
                }
                break;
                case 0x05://IO Advice Hints
                {
                    uint8_t ioAdviceHints[1040 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 1040 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, ioAdviceHints))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (ioAdviceHints[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (ioAdviceHints[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (ioAdviceHints[headerLength + 1] != 0x05)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        //check if any of the Hints valid bits are set so we know it is enabled. TODO: add checking for the cache enabled bit?
                        bool valid = false;
                        for (uint16_t iter = headerLength + 15; iter < (1040 + headerLength); iter += 16)
                        {
                            uint8_t hintsMode = (ioAdviceHints[0] & 0xC0) >> 6;
                            if (hintsMode == 0)
                            {
                                valid = true;
                                break;//we found at least one, so get out of the loop.
                            }
                        }
                        if (valid)
                        {
                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "IO Advice Hints [Enabled]");
                            driveInfo->numberOfFeaturesSupported++;
                        }
                        else
                        {
                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "IO Advice Hints");
                            driveInfo->numberOfFeaturesSupported++;
                        }
                    }
                }
                break;
                case 0xF1://PATA control
                    //if we can read this page, then the device supports PATA Control
                {
                    uint8_t pataControl[8 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 8 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, pataControl))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 8 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, pataControl))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (pataControl[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (pataControl[headerLength + 1] != 0xF1)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "PATA Control");
                        driveInfo->numberOfFeaturesSupported++;
                    }
                }
                break;
                default:
                    break;
                }
                break;
            case MP_PROTOCOL_SPECIFIC_PORT:
                switch (subPageCode)
                {
                case 0x00://Protocol specific port (Use this to get whether SAS or FC or SCSI, etc)
                {
                    uint8_t protocolSpecificPort[LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (protocolSpecificPort[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, UINT8_MAX, subPageCode, true, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (protocolSpecificPort[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (pageRead)
                    {
                        protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 2]);
                    }
                }
                break;
                case 0x01://Phy control and discover mode page (SAS)
                {
                    uint8_t protocolSpecificPort[LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (protocolSpecificPort[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, UINT8_MAX, subPageCode, true, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (protocolSpecificPort[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (protocolSpecificPort[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (protocolSpecificPort[headerLength + 1] != 0x01)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                        switch (protocolIdentifier)
                        {
                        case 0x0://Fiber Channel
                            driveInfo->interfaceSpeedInfo.speedIsValid = true;
                            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_FIBRE;
                            break;
                        case 0x1://parallel scsi
                        case 0x2://serial storage architecture scsi-3 protocol
                        case 0x3://IEEE 1394
                        case 0x4://RDMA 
                        case 0x5://iSCSI
                            break;
                        case 0x6:
                        {
                            driveInfo->interfaceSpeedInfo.speedIsValid = true;
                            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_SERIAL;
                            uint16_t phyDescriptorIter = headerLength + 8;
                            uint16_t phyPageLen = M_BytesTo2ByteValue(protocolSpecificPort[headerLength + 2], protocolSpecificPort[headerLength + 3]);
                            driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts = protocolSpecificPort[headerLength + 7];
                            uint8_t phyCount = 0;
                            //now we need to go through the descriptors for each phy
                            for (; phyDescriptorIter < (uint16_t)M_Min((uint16_t)(phyPageLen + headerLength), (uint16_t)(LEGACY_DRIVE_SEC_SIZE + headerLength)) && phyCount < (uint8_t)MAX_PORTS; phyDescriptorIter += 48, phyCount++)
                            {
                                //uint8_t phyIdentifier = modePages[phyDescriptorIter + 1];
                                switch (M_Nibble0(protocolSpecificPort[phyDescriptorIter + 5]))
                                {
                                case 0x8://1.5 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 1;
                                    break;
                                case 0x9://3.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 2;
                                    break;
                                case 0xA://6.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 3;
                                    break;
                                case 0xB://12.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 4;
                                    break;
                                case 0xC:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 5;
                                    break;
                                case 0xD:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 6;
                                    break;
                                case 0xE:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 7;
                                    break;
                                case 0xF:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 8;
                                    break;
                                default:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 0;
                                    break;
                                }
                                switch (M_Nibble0(protocolSpecificPort[phyDescriptorIter + 33]))
                                {
                                case 0x8://1.5 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 1;
                                    break;
                                case 0x9://3.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 2;
                                    break;
                                case 0xA://6.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 3;
                                    break;
                                case 0xB://12.0 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 4;
                                    break;
                                case 0xC://22.5 Gb/s
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 5;
                                    break;
                                case 0xD:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 6;
                                    break;
                                case 0xE:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 7;
                                    break;
                                case 0xF:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 8;
                                    break;
                                default:
                                    driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 0;
                                    break;
                                }
                            }
                        }
                        break;
                        case 0x7://automation/drive interface transport
                        case 0x8://AT Attachement interface
                        case 0x9://UAS
                        case 0xA://SCSI over PCI Express
                        case 0xB://PCI Express protocols
                        case 0xF://No specific protocol
                        default://reserved
                            break;
                        }
                    }
                }
                case 0x03://Negotiated Settings (Parallel SCSI)
                {
                    uint8_t protocolSpecificPort[LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (protocolSpecificPort[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, UINT8_MAX, subPageCode, true, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (protocolSpecificPort[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (protocolSpecificPort[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (protocolSpecificPort[headerLength + 1] != 0x01)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                        switch (protocolIdentifier)
                        {
                        case 0x0://Fiber Channel
                            break;
                        case 0x1://parallel scsi
                        {
                            //get the negotiated speed
                            uint16_t scalingMultiplier = 0;
                            uint8_t transferPeriodFactor = protocolSpecificPort[headerLength + 6];
                            uint8_t transferWidthExponent = protocolSpecificPort[headerLength + 9];
                            switch (transferPeriodFactor)
                            {
                            case 0x07:
                                scalingMultiplier = 320;
                                break;
                            case 0x08:
                                scalingMultiplier = 160;
                                break;
                            case 0x09:
                                scalingMultiplier = 80;
                                break;
                            case 0x0A:
                                scalingMultiplier = 40;
                                break;
                            case 0x0B:
                                scalingMultiplier = 40;
                                break;
                            case 0x0C:
                                scalingMultiplier = 20;
                                break;
                            default:
                                //need to do an if here...
                                if (transferPeriodFactor >= 0x0D && transferPeriodFactor <= 0x18)
                                {
                                    scalingMultiplier = 20;
                                }
                                else if (transferPeriodFactor >= 0x19 && transferPeriodFactor <= 0x31)
                                {
                                    scalingMultiplier = 10;
                                }
                                else if (transferPeriodFactor >= 0x32 && transferPeriodFactor <= 0xFF)
                                {
                                    scalingMultiplier = 5;
                                }
                                break;
                            }
                            if (scalingMultiplier > 0)
                            {
                                driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
                                driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = scalingMultiplier * (transferWidthExponent + 1);
                                sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, "FAST-%" PRIu16"", scalingMultiplier);
                                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                            }
                        }
                        break;
                        case 0x2://serial storage architecture scsi-3 protocol
                        case 0x3://IEEE 1394
                        case 0x4://RDMA 
                        case 0x5://iSCSI
                        case 0x6:
                        default:
                            break;
                        }
                    }
                }
                case 0x04://Report Transfer Capabilities (Parallel SCSI)
                {
                    uint8_t protocolSpecificPort[LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, LEGACY_DRIVE_SEC_SIZE + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (protocolSpecificPort[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, UINT8_MAX, subPageCode, true, MPC_CURRENT_VALUES, protocolSpecificPort))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (protocolSpecificPort[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (protocolSpecificPort[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (protocolSpecificPort[headerLength + 1] != 0x01)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                        switch (protocolIdentifier)
                        {
                        case 0x0://Fiber Channel
                            break;
                        case 0x1://parallel scsi
                        {
                            //get the max speed
                            uint16_t scalingMultiplier = 0;
                            uint8_t transferPeriodFactor = protocolSpecificPort[headerLength + 6];
                            uint8_t transferWidthExponent = protocolSpecificPort[headerLength + 9];
                            switch (transferPeriodFactor)
                            {
                            case 0x07:
                                scalingMultiplier = 320;
                                break;
                            case 0x08:
                                scalingMultiplier = 160;
                                break;
                            case 0x09:
                                scalingMultiplier = 80;
                                break;
                            case 0x0A:
                                scalingMultiplier = 40;
                                break;
                            case 0x0B:
                                scalingMultiplier = 40;
                                break;
                            case 0x0C:
                                scalingMultiplier = 20;
                                break;
                            default:
                                //need to do an if here...
                                if (transferPeriodFactor >= 0x0D && transferPeriodFactor <= 0x18)
                                {
                                    scalingMultiplier = 20;
                                }
                                else if (transferPeriodFactor >= 0x19 && transferPeriodFactor <= 0x31)
                                {
                                    scalingMultiplier = 10;
                                }
                                else if (transferPeriodFactor >= 0x32 && transferPeriodFactor <= 0xFF)
                                {
                                    scalingMultiplier = 5;
                                }
                                break;
                            }
                            if (scalingMultiplier > 0)
                            {
                                driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
                                driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = scalingMultiplier * (transferWidthExponent + 1);
                                sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "FAST-%" PRIu16"", scalingMultiplier);
                                driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                            }
                        }
                        break;
                        case 0x2://serial storage architecture scsi-3 protocol
                        case 0x3://IEEE 1394
                        case 0x4://RDMA 
                        case 0x5://iSCSI
                        case 0x6:
                        default:
                            break;
                        }
                    }
                }
                default:
                    break;
                }
                break;
            case MP_POWER_CONDTION:
                switch (subPageCode)
                {
                case 0x00://EPC
                {
                    char *epcFeatureString = NULL;
                    //read the default values to check if it's supported...then try the current page...
                    bool readDefaults = false;
                    uint8_t powerConditions[40 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t mpHeaderLen = MODE_PARAMETER_HEADER_10_LEN;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 40 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_DEFAULT_VALUES, powerConditions))
                    {
                        readDefaults = true;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 40 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_DEFAULT_VALUES, powerConditions))
                    {
                        mpHeaderLen = MODE_PARAMETER_HEADER_6_LEN;
                        readDefaults = true;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (readDefaults)
                    {
                        if (powerConditions[1 + mpHeaderLen] > 0x0A)
                        {
                            if (!epcFeatureString)
                            {
                                epcFeatureString = (char*)calloc(4, sizeof(char));
                            }
                            else
                            {
                                epcFeatureString = (char*)realloc(epcFeatureString, 4 * sizeof(char));
                                memset(epcFeatureString, 0, 4);
                            }
                            strcpy(epcFeatureString, "EPC");
                        }
                        else
                        {
                            if (!epcFeatureString)
                            {
                                epcFeatureString = (char*)calloc(17, sizeof(char));
                            }
                            else
                            {
                                epcFeatureString = (char*)realloc(epcFeatureString, 17 * sizeof(char));
                                memset(epcFeatureString, 0, 17);
                            }
                            strcpy(epcFeatureString, "Power Conditions");
                        }
                    }
                    //Now read the current page to see if it's more than just supported :)
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 40 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, powerConditions))
                    {
                        pageRead = true;
                        if (powerConditions[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 40 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, powerConditions))
                    {
                        mpHeaderLen = MODE_PARAMETER_HEADER_6_LEN;
                        pageRead = true;
                        if (powerConditions[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (pageRead)
                    {
                        if (powerConditions[1 + mpHeaderLen] > 0x0A &&
                            (powerConditions[2 + mpHeaderLen] & BIT0 ||
                                powerConditions[3 + mpHeaderLen] & BIT0 ||
                                powerConditions[3 + mpHeaderLen] & BIT1 ||
                                powerConditions[3 + mpHeaderLen] & BIT2 ||
                                powerConditions[3 + mpHeaderLen] & BIT3
                                )
                            )
                        {
                            if (!epcFeatureString)
                            {
                                epcFeatureString = (char*)calloc(14, sizeof(char));
                            }
                            else
                            {
                                epcFeatureString = (char*)realloc(epcFeatureString, 14 * sizeof(char));
                                memset(epcFeatureString, 0, 14);
                            }
                            strcpy(epcFeatureString, "EPC [Enabled]");
                        }
                        else if (powerConditions[3 + mpHeaderLen] & BIT0 || powerConditions[3 + mpHeaderLen] & BIT1)
                        {
                            if (!epcFeatureString)
                            {
                                epcFeatureString = (char*)calloc(27, sizeof(char));
                            }
                            else
                            {
                                epcFeatureString = (char*)realloc(epcFeatureString, 27 * sizeof(char));
                                memset(epcFeatureString, 0, 27);
                            }
                            strcpy(epcFeatureString, "Power Conditions [Enabled]");
                        }
                    }
                    if (epcFeatureString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", epcFeatureString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    safe_Free(epcFeatureString);
                }
                break;
                case 0xF1://ata power conditions
                {
                    uint8_t ataPowerConditions[16 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t mpHeaderLen = MODE_PARAMETER_HEADER_10_LEN;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 16 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, ataPowerConditions))
                    {
                        pageRead = true;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 16 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, ataPowerConditions))
                    {
                        mpHeaderLen = MODE_PARAMETER_HEADER_6_LEN;
                        pageRead = true;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (ataPowerConditions[mpHeaderLen + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (ataPowerConditions[mpHeaderLen + 1] != 0xF1)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        if (ataPowerConditions[mpHeaderLen + 0x05] & BIT0)
                        {
                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "APM [Enabled]");
                            driveInfo->numberOfFeaturesSupported++;
                        }
                        else
                        {
                            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "APM");
                            driveInfo->numberOfFeaturesSupported++;
                        }
                    }
                }
                break;
                default:
                    break;
                }
                break;
            case MP_INFORMATION_EXCEPTIONS_CONTROL:
                switch (subPageCode)
                {
                case 0:
                {
                    uint8_t informationalExceptions[12 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 12 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, informationalExceptions))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (informationalExceptions[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 12 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, informationalExceptions))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (informationalExceptions[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    if (pageRead)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Informational Exceptions [Mode %"PRIu8"]", M_Nibble0(informationalExceptions[headerLength + 3]));
                        driveInfo->numberOfFeaturesSupported++;
                    }
                }
                break;
                case 0x01://Background control
                {
                    //check if DLC is supported or can be changed before checking if they are enabled or not.
                    char *bmsString = NULL;
                    char *bmsPSString = NULL;
                    uint8_t backgroundControl[16 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };//need to include header length in this
                    bool pageRead = false, defaultsRead = false;
                    uint8_t headerLength = 0;
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 16 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_DEFAULT_VALUES, backgroundControl))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 16 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_DEFAULT_VALUES, backgroundControl))
                    {
                        defaultsRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (backgroundControl[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (backgroundControl[headerLength + 1] != 0xF1)
                        {
                            defaultsRead = false;
                        }
                    }
                    else
                    {
                        defaultsRead = false;
                    }
                    if (defaultsRead)
                    {
                        //bms
                        if (backgroundControl[headerLength + 4] & BIT0)
                        {
                            if (!bmsString)
                            {
                                bmsString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                bmsString = (char*)realloc(bmsString, 50 * sizeof(char));
                                memset(bmsString, 0, 50);
                            }
                            strcpy(bmsString, "Background Media Scan");
                        }
                        //bms-ps
                        if (backgroundControl[headerLength + 5] & BIT0)
                        {
                            if (!bmsPSString)
                            {
                                bmsPSString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                bmsPSString = (char*)realloc(bmsPSString, 50 * sizeof(char));
                                memset(bmsPSString, 0, 50);
                            }
                            strcpy(bmsPSString, "Background Pre-Scan");
                        }
                    }
                    if (version >= 2 && SUCCESS == scsi_Mode_Sense_10(device, pageCode, 16 + MODE_PARAMETER_HEADER_10_LEN, subPageCode, true, false, MPC_CURRENT_VALUES, backgroundControl))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                        if (backgroundControl[3] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (SUCCESS == scsi_Mode_Sense_6(device, pageCode, 16 + MODE_PARAMETER_HEADER_6_LEN, subPageCode, true, MPC_CURRENT_VALUES, backgroundControl))
                    {
                        pageRead = true;
                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                        if (backgroundControl[2] & BIT7)
                        {
                            driveInfo->isWriteProtected = true;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    //make sure we got the expected format!
                    if (backgroundControl[headerLength + 0] & BIT6)
                    {
                        //subpage format valid!
                        //check subpage code is valid!
                        if (backgroundControl[headerLength + 1] != 0xF1)
                        {
                            pageRead = false;
                        }
                    }
                    else
                    {
                        pageRead = false;
                    }
                    if (pageRead)
                    {
                        //bms
                        if (backgroundControl[headerLength + 4] & BIT0)
                        {
                            if (!bmsString)
                            {
                                bmsString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                bmsString = (char*)realloc(bmsString, 50 * sizeof(char));
                                memset(bmsString, 0, 50);
                            }
                            strcpy(bmsString, "Background Media Scan [Enabled]");
                        }
                        //bms-ps
                        if (backgroundControl[headerLength + 5] & BIT0)
                        {
                            if (!bmsPSString)
                            {
                                bmsPSString = (char*)calloc(50, sizeof(char));
                            }
                            else
                            {
                                bmsPSString = (char*)realloc(bmsPSString, 50 * sizeof(char));
                                memset(bmsPSString, 0, 50);
                            }
                            strcpy(bmsPSString, "Background Pre-Scan [Enabled]");
                        }
                    }
                    if (bmsString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", bmsString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    if (bmsPSString)
                    {
                        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "%s", bmsPSString);
                        driveInfo->numberOfFeaturesSupported++;
                    }
                    safe_Free(bmsString);
                    safe_Free(bmsPSString);
                }
                break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
    }
    if (!driveInfo->interfaceSpeedInfo.speedIsValid)
    {
        //these old standards didn't report it, but we can reasonably guess the speed
        if (isSCSI1drive)
        {
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.0;
            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "FAST-5");
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
        }
        if (isSCSI2drive)
        {
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 10.0;
            sprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, "FAST-10");
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
        }
    }

    //Read supported Diagnostic parameters and check for rebuild assist. (need SCSI2 and higher since before that, this is all vendor unique)
    uint8_t *supportedDiagnostics = tempBuf;
    memset(supportedDiagnostics, 0, 1024);
    bool pageCodeValid = false;
    uint8_t pageCode = 0;
    if (version >= 3)//PCV bit and page code fields introduced in SPC specification
    {
        pageCodeValid = true;
        pageCode = DIAG_PAGE_SUPPORTED_PAGES;
    }
    //transfer only 4 bytes to the drive for the page format data so we can read the supported pages, then read back the supported list with the receive diagnostics command
    if (version >= 2 && SUCCESS == scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 4, supportedDiagnostics, 4, 15) && SUCCESS == scsi_Receive_Diagnostic_Results(device, pageCodeValid, pageCode, 1024, supportedDiagnostics, 15))
    {
        uint16_t pageLength = M_BytesTo2ByteValue(supportedDiagnostics[2], supportedDiagnostics[3]);
        for (uint16_t iter = 4; iter < (pageLength + 4); ++iter)
        {
            switch (supportedDiagnostics[iter])
            {
            //Add more diagnostic pages in here if we want to check them for supported features.
            case DIAG_PAGE_TRANSLATE_ADDRESS:
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Translate Address");
                driveInfo->numberOfFeaturesSupported++;
                break;
            case DIAG_PAGE_REBUILD_ASSIST:
                //TODO: check and see if the rebuild assist feature is enabled.
                sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Rebuild Assist");
                driveInfo->numberOfFeaturesSupported++;
                break;
            case 0x90:
                if (is_Seagate_Family(device) == SEAGATE)
                {
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Seagate Remanufacture");
                    driveInfo->numberOfFeaturesSupported++;
                    break;
                }
                break;
            case 0x98:
                if (is_Seagate_Family(device) == SEAGATE)
                {
                    sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Seagate In Drive Diagnostics (IDD)");
                    driveInfo->numberOfFeaturesSupported++;
                    break;
                }
                break;
            default:
                break;
            }
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }

    uint8_t *supportedCommands = tempBuf;
    memset(supportedCommands, 0, 1024);

    //Most SAT devices won't report all at once, so try asking for individual commands that are supported
    //one at a time instead of asking for everything all at once.
    //Format unit
    bool formatSupported = false;
    bool fastFormatSupported = false;
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, SCSI_FORMAT_UNIT_CMD, 0, 10, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            formatSupported = true;
            //now check for fast format support
            if (!(supportedCommands[7] == 0xFF && supportedCommands[8] == 0xFF))//if both these bytes are FFh, then the drive conforms to SCSI2 where this was the "interleave" field
            {
                if (supportedCommands[8] & 0x03)//checks that fast format bits are available for use.
                {
                    fastFormatSupported = true;
                }
            }
            break;
        default:
            break;
        }
    }
    else if (version >= 3 && version < 5 && SUCCESS == scsi_Inquiry(device, supportedCommands, 12, SCSI_FORMAT_UNIT_CMD, false, true))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            formatSupported = true;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (formatSupported)
    {
        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Format Unit");
        driveInfo->numberOfFeaturesSupported++;
    }
    if (fastFormatSupported)
    {
        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Fast Format");
        driveInfo->numberOfFeaturesSupported++;
    }
    memset(supportedCommands, 0, 1024);
    //Sanitize (need to check each service action to make sure at least one is supported.
    bool sanitizeSupported = false;
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_OVERWRITE, 14, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sanitizeSupported = true;
            break;
        default:
            break;
        }
    }
    else if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_BLOCK_ERASE, 14, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sanitizeSupported = true;
            break;
        default:
            break;
        }
    }
    else if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE, 14, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sanitizeSupported = true;
            break;
        default:
            break;
        }
    }
    else if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_EXIT_FAILURE_MODE, 14, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sanitizeSupported = true;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (sanitizeSupported)
    {
        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Sanitize");
        driveInfo->numberOfFeaturesSupported++;
    }
    //storage element depopulation
    bool getElementStatusSupported = false;
    bool removeAndTruncateSupported = false;
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x17, 20, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            getElementStatusSupported = true;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x18, 20, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            removeAndTruncateSupported = true;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (removeAndTruncateSupported && getElementStatusSupported)
    {
        sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "Storage Element Depopulation");
        driveInfo->numberOfFeaturesSupported++;
    }

    //check write buffer (firmware download) call info firmware download.h for this information.
    supportedDLModes supportedDLModes;
    memset(&supportedDLModes, 0, sizeof(supportedDLModes));
    //change the device type to scsi before we enter here! Doing this so that --satinfo is correct!
    int tempDevType = device->drive_info.drive_type;
    device->drive_info.drive_type = SCSI_DRIVE;
    if (SUCCESS == get_Supported_FWDL_Modes(device, &supportedDLModes))
    {
        driveInfo->fwdlSupport.downloadSupported = supportedDLModes.downloadMicrocodeSupported;
        driveInfo->fwdlSupport.segmentedSupported = supportedDLModes.segmented;
        driveInfo->fwdlSupport.deferredSupported = supportedDLModes.deferred;
        driveInfo->fwdlSupport.dmaModeSupported = supportedDLModes.firmwareDownloadDMACommandSupported;
        driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired = supportedDLModes.seagateDeferredPowerCycleActivate;
    }
    device->drive_info.drive_type = tempDevType;
    if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    //ATA Passthrough commands
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, ATA_PASS_THROUGH_12, 0, 16, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            //TODO: make sure this isn't the "blank" command being supported by a MMC device.
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "ATA Pass-Through 12");
            driveInfo->numberOfFeaturesSupported++;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, ATA_PASS_THROUGH_16, 0, 20, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "ATA Pass-Through 16");
            driveInfo->numberOfFeaturesSupported++;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    if (version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x7F, 0x1FF0, 36, supportedCommands))
    {
        switch (supportedCommands[1] & 0x07)
        {
        case 0: //not available right now...so not supported
        case 1://not supported
            break;
        case 3://supported according to spec
        case 5://supported in vendor specific mannor in same format as case 3
            sprintf(driveInfo->featuresSupported[driveInfo->numberOfFeaturesSupported], "ATA Pass-Through 32");
            driveInfo->numberOfFeaturesSupported++;
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
    {
        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
        scsi_Test_Unit_Ready(device, NULL);
    }
    driveInfo->lowCurrentSpinupValid = false;
    safe_Free(tempBuf);
    return ret;
}

int get_NVMe_Drive_Information(tDevice *device, ptrDriveInformationNVMe driveInfo)
{
    int ret = NOT_SUPPORTED;
    memset(driveInfo, 0, sizeof(driveInformationNVMe));
#if !defined(DISABLE_NVME_PASSTHROUGH)
    //changing ret to success since we have passthrough available
    ret = SUCCESS;
    uint8_t *nvmeIdentifyData = (uint8_t*)calloc(NVME_IDENTIFY_DATA_LEN, sizeof(uint8_t));
    if (SUCCESS == nvme_Identify(device, nvmeIdentifyData, 0, 1))
    {
        //MN
        memcpy(driveInfo->controllerData.modelNumber, &nvmeIdentifyData[24], 40);
        remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.modelNumber);
        //SN
        memcpy(driveInfo->controllerData.serialNumber, &nvmeIdentifyData[4], 20);
        remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.serialNumber);
        //FW
        memcpy(driveInfo->controllerData.firmwareRevision, &nvmeIdentifyData[64], 8);
        remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.firmwareRevision);
        //vid
        driveInfo->controllerData.pciVendorID = M_BytesTo2ByteValue(nvmeIdentifyData[1], nvmeIdentifyData[0]);
        //ssvid
        driveInfo->controllerData.pciSubsystemVendorID = M_BytesTo2ByteValue(nvmeIdentifyData[3], nvmeIdentifyData[2]);
        //IEEE OUI
        driveInfo->controllerData.ieeeOUI = M_BytesTo4ByteValue(0, nvmeIdentifyData[75], nvmeIdentifyData[74], nvmeIdentifyData[73]);
        //controller ID
        driveInfo->controllerData.controllerID = M_BytesTo2ByteValue(nvmeIdentifyData[79], nvmeIdentifyData[78]);
        //version
        driveInfo->controllerData.majorVersion = M_BytesTo2ByteValue(nvmeIdentifyData[83], nvmeIdentifyData[82]);
        driveInfo->controllerData.minorVersion = nvmeIdentifyData[81];
        driveInfo->controllerData.tertiaryVersion = nvmeIdentifyData[80];
        if (nvmeIdentifyData[96] & BIT0)
        {
            driveInfo->controllerData.hostIdentifierSupported = true;
            //host identifier is supported
            nvmeFeaturesCmdOpt getHostIdentifier;
            memset(&getHostIdentifier, 0, sizeof(nvmeFeaturesCmdOpt));
            getHostIdentifier.fid = 0x81;
            getHostIdentifier.sel = 0;//current data
            uint8_t hostIdentifier[16] = { 0 };
            getHostIdentifier.prp1 = (uint64_t)hostIdentifier;
            //TODO: Need to debug why this doesn't work right now - TJE
            if (SUCCESS == nvme_Get_Features(device, &getHostIdentifier))
            {
                memcpy(&driveInfo->controllerData.hostIdentifier, hostIdentifier, 16);
                if (getHostIdentifier.featSetGetValue & BIT0)
                {
                    driveInfo->controllerData.hostIdentifierIs128Bits = true;
                }
            }
        }
        //fguid (This field is big endian)
        driveInfo->controllerData.fguid[0] = nvmeIdentifyData[112];
        driveInfo->controllerData.fguid[1] = nvmeIdentifyData[113];
        driveInfo->controllerData.fguid[2] = nvmeIdentifyData[114];
        driveInfo->controllerData.fguid[3] = nvmeIdentifyData[115];
        driveInfo->controllerData.fguid[4] = nvmeIdentifyData[116];
        driveInfo->controllerData.fguid[5] = nvmeIdentifyData[117];
        driveInfo->controllerData.fguid[6] = nvmeIdentifyData[118];
        driveInfo->controllerData.fguid[7] = nvmeIdentifyData[119];
        driveInfo->controllerData.fguid[8] = nvmeIdentifyData[120];
        driveInfo->controllerData.fguid[9] = nvmeIdentifyData[121];
        driveInfo->controllerData.fguid[10] = nvmeIdentifyData[122];
        driveInfo->controllerData.fguid[11] = nvmeIdentifyData[123];
        driveInfo->controllerData.fguid[12] = nvmeIdentifyData[124];
        driveInfo->controllerData.fguid[13] = nvmeIdentifyData[125];
        driveInfo->controllerData.fguid[14] = nvmeIdentifyData[126];
        driveInfo->controllerData.fguid[15] = nvmeIdentifyData[127];
        //warning composite temperature
        driveInfo->controllerData.warningCompositeTemperatureThreshold = M_BytesTo2ByteValue(nvmeIdentifyData[267], nvmeIdentifyData[266]);
        //critical composite temperature
        driveInfo->controllerData.criticalCompositeTemperatureThreshold = M_BytesTo2ByteValue(nvmeIdentifyData[269], nvmeIdentifyData[268]);
        //total nvm capacity
        for (uint8_t i = 0; i < 16; ++i)
        {
            driveInfo->controllerData.totalNVMCapacity[i] = nvmeIdentifyData[295 + i];
        }
        driveInfo->controllerData.totalNVMCapacityD = convert_128bit_to_double(&driveInfo->controllerData.totalNVMCapacity[0]);
        //unallocated nvm capacity
        for (uint8_t i = 0; i < 16; ++i)
        {
            driveInfo->controllerData.unallocatedNVMCapacity[i] = nvmeIdentifyData[296 + i];
        }
        driveInfo->controllerData.unallocatedNVMCapacityD = convert_128bit_to_double(&driveInfo->controllerData.unallocatedNVMCapacity[0]);
        //DST info
        if (nvmeIdentifyData[256] & BIT4)//DST command is supported
        {
            //set Long DST Time before reading the log
            driveInfo->controllerData.longDSTTimeMinutes = M_BytesTo2ByteValue(nvmeIdentifyData[317], nvmeIdentifyData[316]);
            //Read the NVMe DST log
            uint8_t nvmeDSTLog[564] = { 0 };
            nvmeGetLogPageCmdOpts dstLogOpts;
            memset(&dstLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
            dstLogOpts.addr = nvmeDSTLog;
            dstLogOpts.dataLen = 564;
            dstLogOpts.lid = 6;
            dstLogOpts.nsid = 0;//controller data
            if (SUCCESS == nvme_Get_Log_Page(device, &dstLogOpts))
            {
                driveInfo->dstInfo.informationValid = true;
                if (nvmeDSTLog[0] == 0)
                {
                    //DST has never been run
                    //TODO: debug if we need to set fields to a different value
                }
                else
                {
                    //Bytes 31:4 hold the latest DST run information
                    uint32_t latestDSTOffset = 4;
                    driveInfo->dstInfo.testNumber = M_Nibble1(nvmeDSTLog[latestDSTOffset + 0]);
                    driveInfo->dstInfo.resultOrStatus = M_Nibble0(nvmeDSTLog[latestDSTOffset + 0]);
                    driveInfo->dstInfo.powerOnHours = M_BytesTo8ByteValue(nvmeDSTLog[latestDSTOffset + 11], nvmeDSTLog[latestDSTOffset + 10], nvmeDSTLog[latestDSTOffset + 9], nvmeDSTLog[latestDSTOffset + 8], nvmeDSTLog[latestDSTOffset + 7], nvmeDSTLog[latestDSTOffset + 6], nvmeDSTLog[latestDSTOffset + 5], nvmeDSTLog[latestDSTOffset + 4]);
                    if (nvmeDSTLog[latestDSTOffset + 2] & BIT1)
                    {
                        //TODO: namespace with the error?
                        driveInfo->dstInfo.errorLBA = M_BytesTo8ByteValue(nvmeDSTLog[latestDSTOffset + 23], nvmeDSTLog[latestDSTOffset + 22], nvmeDSTLog[latestDSTOffset + 12], nvmeDSTLog[latestDSTOffset + 20], nvmeDSTLog[latestDSTOffset + 19], nvmeDSTLog[latestDSTOffset + 18], nvmeDSTLog[latestDSTOffset + 17], nvmeDSTLog[latestDSTOffset + 16]);
                    }
                    else
                    {
                        driveInfo->dstInfo.errorLBA = UINT64_MAX;
                    }
                }
            }
        }
        //Sanitize
        if (nvmeIdentifyData[328] & BIT0)//Sanitize supported
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Sanitize");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        //max namespaces
        driveInfo->controllerData.maxNumberOfNamespaces = M_BytesTo4ByteValue(nvmeIdentifyData[519], nvmeIdentifyData[518], nvmeIdentifyData[517], nvmeIdentifyData[516]);
        //volatile write cache
        if (nvmeIdentifyData[525] & BIT0)
        {
            driveInfo->controllerData.volatileWriteCacheSupported = true;
            nvmeFeaturesCmdOpt getWriteCache;
            memset(&getWriteCache, 0, sizeof(nvmeFeaturesCmdOpt));
            getWriteCache.fid = 0x06;
            getWriteCache.sel = 0;//current data
            if (SUCCESS == nvme_Get_Features(device, &getWriteCache))
            {
                if (getWriteCache.featSetGetValue & BIT0)
                {
                    driveInfo->controllerData.volatileWriteCacheEnabled = true;
                }
                else
                {
                    driveInfo->controllerData.volatileWriteCacheEnabled = false;
                }
            }
            else
            {
                driveInfo->controllerData.volatileWriteCacheSupported = false;
            }
        }
        //nvm subsystem qualified name
        memcpy(driveInfo->controllerData.nvmSubsystemNVMeQualifiedName, &nvmeIdentifyData[768], 256);
        //firmware slots
        driveInfo->controllerData.numberOfFirmwareSlots = M_GETBITRANGE(nvmeIdentifyData[260], 3, 1);
        //TODO: Add in other controller "Features"
        if (nvmeIdentifyData[256] & BIT0)
        {
            //Supports security send/receive. Check for TCG and other security protocols
            uint8_t supportedSecurityProtocols[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == nvme_Security_Receive(device, SECURITY_PROTOCOL_INFORMATION, 0, 0, supportedSecurityProtocols, 512))
            {
                bool tcgFeatureFound = false;
                uint16_t length = M_BytesTo2ByteValue(supportedSecurityProtocols[6], supportedSecurityProtocols[7]);
                uint16_t bufIter = 8;
                for (; (bufIter - 8) < length; bufIter++)
                {
                    switch (supportedSecurityProtocols[bufIter])
                    {
                    case SECURITY_PROTOCOL_INFORMATION:
                        break;
                    case SECURITY_PROTOCOL_TCG_1:
                    case SECURITY_PROTOCOL_TCG_2:
                    case SECURITY_PROTOCOL_TCG_3:
                    case SECURITY_PROTOCOL_TCG_4:
                    case SECURITY_PROTOCOL_TCG_5:
                    case SECURITY_PROTOCOL_TCG_6:
                        driveInfo->controllerData.encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                        if (!tcgFeatureFound)
                        {
                            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "TCG");
                            ++(driveInfo->controllerData.numberOfControllerFeatures);
                            tcgFeatureFound = true;
                        }
                        break;
                    case SECURITY_PROTOCOL_CbCS:
                    case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
                    case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
                    case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
                    case SECURITY_PROTOCOL_IKE_V2_SCSI:
                    case SECURITY_PROTOCOL_NVM_EXPRESS:
                        break;
                    case SECURITY_PROTOCOL_SCSA:
                        sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "SCSA");
                        ++(driveInfo->controllerData.numberOfControllerFeatures);
                        break;
                    case SECURITY_PROTOCOL_JEDEC_UFS:
                    case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
                        break;
                    case SECURITY_PROTOCOL_IEEE_1667:
                        sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "IEEE1667");
                        ++(driveInfo->controllerData.numberOfControllerFeatures);
                        break;
                    case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
                    {
                        //We shouldn't see the ATA security protocol on NVMe, but according to some internet searches, some NVMe drive have implemented this. I'll just leave it commented out since it is definitely outside of the standard. - TJE
                        ////read the data from this page to set ATA security information
                        //uint8_t ataSecurityInfo[16] = { 0 };
                        //if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0, false, 16, ataSecurityInfo))
                        //{
                        //    driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[2], ataSecurityInfo[3]);
                        //    driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[4], ataSecurityInfo[5]);
                        //    driveInfo->ataSecurityInformation.masterPasswordIdentifier = M_BytesTo2ByteValue(ataSecurityInfo[6], ataSecurityInfo[7]);
                        //    //check the bits now
                        //    if (ataSecurityInfo[8] & BIT0)
                        //    {
                        //        driveInfo->ataSecurityInformation.masterPasswordCapability = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT5)
                        //    {
                        //        driveInfo->ataSecurityInformation.enhancedEraseSupported = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT4)
                        //    {
                        //        driveInfo->ataSecurityInformation.securityCountExpired = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT3)
                        //    {
                        //        driveInfo->ataSecurityInformation.securityFrozen = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT2)
                        //    {
                        //        driveInfo->ataSecurityInformation.securityLocked = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT1)
                        //    {
                        //        driveInfo->ataSecurityInformation.securityEnabled = true;
                        //    }
                        //    if (ataSecurityInfo[9] & BIT0)
                        //    {
                        //        driveInfo->ataSecurityInformation.securitySupported = true;
                        //    }
                        //}
                        sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "ATA Security");
                        ++(driveInfo->controllerData.numberOfControllerFeatures);
                    }
                    break;
                    default:
                        break;
                    }
                }
            }
        }
        if (nvmeIdentifyData[256] & BIT1)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Format NVM");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        if (nvmeIdentifyData[256] & BIT2)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Firmware Update");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        if (nvmeIdentifyData[256] & BIT3)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Namespace Management");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        if (nvmeIdentifyData[256] & BIT4)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Device Self Test");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        if (nvmeIdentifyData[256] & BIT7)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Virtualization Management");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }
        if (nvmeIdentifyData[257] & BIT1)
        {
            sprintf(driveInfo->controllerData.controllerFeaturesSupported[driveInfo->controllerData.numberOfControllerFeatures], "Doorbell Buffer Config");
            ++(driveInfo->controllerData.numberOfControllerFeatures);
        }

        //Before we memset the identify data, add some namespace features
        if (nvmeIdentifyData[520] & BIT1)
        {
            sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Write Uncorrectable");
            ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
        }
        if (nvmeIdentifyData[520] & BIT2)
        {
            sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Dataset Management");
            ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
        }
        if (nvmeIdentifyData[520] & BIT3)
        {
            sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Write Zeros");
            ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
        }
        
        memset(nvmeIdentifyData, 0, NVME_IDENTIFY_DATA_LEN);
        if (SUCCESS == nvme_Identify(device, nvmeIdentifyData, device->drive_info.namespaceID, 0))
        {
            driveInfo->namespaceData.valid = true;
            driveInfo->namespaceData.namespaceSize = M_BytesTo8ByteValue(nvmeIdentifyData[7], nvmeIdentifyData[6], nvmeIdentifyData[5], nvmeIdentifyData[4], nvmeIdentifyData[3], nvmeIdentifyData[2], nvmeIdentifyData[1], nvmeIdentifyData[0]) - 1;//spec says this is 0 to (n-1)!
            driveInfo->namespaceData.namespaceCapacity = M_BytesTo8ByteValue(nvmeIdentifyData[15], nvmeIdentifyData[14], nvmeIdentifyData[13], nvmeIdentifyData[12], nvmeIdentifyData[11], nvmeIdentifyData[10], nvmeIdentifyData[9], nvmeIdentifyData[8]);
            driveInfo->namespaceData.namespaceUtilization = M_BytesTo8ByteValue(nvmeIdentifyData[23], nvmeIdentifyData[22], nvmeIdentifyData[21], nvmeIdentifyData[20], nvmeIdentifyData[19], nvmeIdentifyData[18], nvmeIdentifyData[17], nvmeIdentifyData[16]);
            //lba size & relative performance
            uint8_t lbaFormatIdentifier = M_Nibble0(nvmeIdentifyData[26]);
            //lba formats start at byte 128, and are 4 bytes in size each
            uint32_t lbaFormatOffset = 128 + (lbaFormatIdentifier * 4);
            uint32_t lbaFormatData = M_BytesTo4ByteValue(nvmeIdentifyData[lbaFormatOffset + 3], nvmeIdentifyData[lbaFormatOffset + 2], nvmeIdentifyData[lbaFormatOffset + 1], nvmeIdentifyData[lbaFormatOffset + 0]);
            driveInfo->namespaceData.formattedLBASizeBytes = (uint32_t)power_Of_Two(M_GETBITRANGE(lbaFormatData, 23, 16));
            driveInfo->namespaceData.relativeFormatPerformance = M_GETBITRANGE(lbaFormatData, 25, 24);
            //nvm capacity
            for (uint8_t i = 0; i < 16; ++i)
            {
                driveInfo->namespaceData.nvmCapacity[i] = nvmeIdentifyData[48 + i];
            }
            driveInfo->namespaceData.nvmCapacityD = convert_128bit_to_double(&driveInfo->namespaceData.nvmCapacity[0]);
            //NGUID
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[0] = nvmeIdentifyData[104];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[1] = nvmeIdentifyData[105];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[2] = nvmeIdentifyData[106];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[3] = nvmeIdentifyData[107];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[4] = nvmeIdentifyData[108];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[5] = nvmeIdentifyData[109];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[6] = nvmeIdentifyData[110];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[7] = nvmeIdentifyData[111];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[8] = nvmeIdentifyData[112];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[9] = nvmeIdentifyData[113];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[10] = nvmeIdentifyData[114];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[11] = nvmeIdentifyData[115];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[12] = nvmeIdentifyData[116];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[13] = nvmeIdentifyData[117];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[14] = nvmeIdentifyData[118];
            driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[15] = nvmeIdentifyData[119];
            //EUI64
            driveInfo->namespaceData.ieeeExtendedUniqueIdentifier = M_BytesTo8ByteValue(nvmeIdentifyData[120], nvmeIdentifyData[121], nvmeIdentifyData[122], nvmeIdentifyData[123], nvmeIdentifyData[124], nvmeIdentifyData[125], nvmeIdentifyData[126], nvmeIdentifyData[127]);
            //TODO: Namespace "features"
            uint8_t protectionEnabled = M_GETBITRANGE(nvmeIdentifyData[29], 2, 0);
            if (nvmeIdentifyData[28] & BIT0)
            {
                if (protectionEnabled == 1)
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 1 [Enabled]");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
                else
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 1");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
            }
            if (nvmeIdentifyData[28] & BIT1)
            {
                if (protectionEnabled == 2)
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 2 [Enabled]");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
                else
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 2");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
            }
            if (nvmeIdentifyData[28] & BIT2)
            {
                if (protectionEnabled == 3)
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 3 [Enabled]");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
                else
                {
                    sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Protection Type 3");
                    ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
                }
            }
            if (nvmeIdentifyData[30] & BIT0)
            {
                sprintf(driveInfo->namespaceData.namespaceFeaturesSupported[driveInfo->namespaceData.numberOfNamespaceFeatures], "Namespace Sharing");
                ++(driveInfo->namespaceData.numberOfNamespaceFeatures);
            }
        }
        //Data from SMART log page
        uint8_t nvmeSMARTData[512] = { 0 };
        nvmeGetLogPageCmdOpts smartLogOpts;
        memset(&smartLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        smartLogOpts.addr = nvmeSMARTData;
        smartLogOpts.dataLen = 512;
        smartLogOpts.lid = 2;
        smartLogOpts.nsid = NVME_ALL_NAMESPACES;//controller data
        if (SUCCESS == nvme_Get_Log_Page(device, &smartLogOpts))
        {
            driveInfo->smartData.valid = true;
            if (nvmeSMARTData[0] == 0)
            {
                driveInfo->smartData.smartStatus = 0;
            }
            else
            {
                driveInfo->smartData.smartStatus = 1;
            }
            if (nvmeSMARTData[0] & BIT3)
            {
                driveInfo->smartData.mediumIsReadOnly = true;
            }
            driveInfo->smartData.compositeTemperatureKelvin = M_BytesTo2ByteValue(nvmeSMARTData[2], nvmeSMARTData[1]);
            driveInfo->smartData.availableSpacePercent = nvmeSMARTData[3];
            driveInfo->smartData.availableSpaceThresholdPercent = nvmeSMARTData[4];
            driveInfo->smartData.percentageUsed = nvmeSMARTData[5];
            //data units read/written
            for (uint8_t i = 0; i < 16; ++i)
            {
                driveInfo->smartData.dataUnitsRead[i] = nvmeSMARTData[32 + i];
                driveInfo->smartData.dataUnitsWritten[i] = nvmeSMARTData[48 + i];
                driveInfo->smartData.powerOnHours[i] = nvmeSMARTData[128 + i];
            }
            driveInfo->smartData.dataUnitsReadD = convert_128bit_to_double(driveInfo->smartData.dataUnitsRead);
            driveInfo->smartData.dataUnitsWrittenD = convert_128bit_to_double(driveInfo->smartData.dataUnitsWritten);
            driveInfo->smartData.powerOnHoursD = convert_128bit_to_double(driveInfo->smartData.powerOnHours);
        }
        else
        {
            driveInfo->smartData.smartStatus = 2;
        }
    }
    else
    {
        ret = FAILURE;
    }
#endif
    return ret;
}


//This is for use with ATA or SCSI drives where we only want to show the applicable information for each drive type. NOT RECOMMENDED ON EXTERNAL USB/IEEE1394 PRODUCTS!
void print_Device_Information(ptrDriveInformation driveInfo)
{
    switch (driveInfo->infoType)
    {
    case DRIVE_INFO_SAS_SATA:
        print_SAS_Sata_Device_Information(&driveInfo->sasSata);
        break;
    case DRIVE_INFO_NVME:
        print_NVMe_Device_Information(&driveInfo->nvme);
        break;
    default:
        break;
    }
}

void print_NVMe_Device_Information(ptrDriveInformationNVMe driveInfo)
{
    printf("NVMe Controller Information:\n");
    printf("\tModel Number: %s\n", driveInfo->controllerData.modelNumber);
    printf("\tSerial Number: %s\n", driveInfo->controllerData.serialNumber);
    printf("\tFirmware Revision: %s\n", driveInfo->controllerData.firmwareRevision);
    printf("\tIEEE OUI: ");
    if (driveInfo->controllerData.ieeeOUI > 0)
    {
        printf("%06" PRIX32 "\n", driveInfo->controllerData.ieeeOUI);
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tPCI Vendor ID: %04" PRIX16 "\n", driveInfo->controllerData.pciVendorID);
    printf("\tPCI Subsystem Vendor ID: %04" PRIX16 "\n", driveInfo->controllerData.pciSubsystemVendorID);
    printf("\tController ID: ");
    if (driveInfo->controllerData.controllerID > 0)
    {
        printf("%04" PRIX16 "\n", driveInfo->controllerData.controllerID);
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tNVMe Version: ");
    if (driveInfo->controllerData.majorVersion > 0 || driveInfo->controllerData.minorVersion > 0 || driveInfo->controllerData.tertiaryVersion > 0)
    {
        printf("%" PRIu16 ".%" PRIu8 ".%" PRIu8 "\n", driveInfo->controllerData.majorVersion, driveInfo->controllerData.minorVersion, driveInfo->controllerData.tertiaryVersion);
    }
    else
    {
        printf("Not reported (NVMe 1.1 or older)\n");
    }
    if (driveInfo->controllerData.hostIdentifierSupported)
    {
        //TODO: Print out the host identifier
    }
    printf("\tFGUID: ");
    uint8_t zero128Bit[16] = { 0 };
    if (memcmp(zero128Bit, driveInfo->controllerData.fguid, 16))
    {
        for (uint8_t i = 0; i < 16; ++i)
        {
            printf("%02" PRIX8, driveInfo->controllerData.fguid[i]);
        }
        printf("\n");
    }
    else
    {
        printf("Not Supported\n");
    }
    if (driveInfo->controllerData.totalNVMCapacityD > 0)
    {
        //convert this to an "easy" unit instead of tons and tons of bytes
        char mTotalCapUnits[4] = { 0 }, totalCapUnits[4] = { 0 };
        char *mTotalCapUnit = &mTotalCapUnits[0], *totalCapUnit = &totalCapUnits[0];
        double mTotalCapacity = driveInfo->controllerData.totalNVMCapacityD;
        double totalCapacity = mTotalCapacity;
        metric_Unit_Convert(&mTotalCapacity, &mTotalCapUnit);
        capacity_Unit_Convert(&totalCapacity, &totalCapUnit);
        printf("\tTotal NVM Capacity (%s/%s): %0.02f/%0.02f\n", mTotalCapUnit, totalCapUnit, mTotalCapacity, totalCapacity);
        if (driveInfo->controllerData.unallocatedNVMCapacityD)
        {
            char mUnCapUnits[4] = { 0 }, unCapUnits[4] = { 0 };
            char *mUnCapUnit = &mUnCapUnits[0], *unCapUnit = &unCapUnits[0];
            double mUnCapacity = driveInfo->controllerData.unallocatedNVMCapacityD;
            double unCapacity = mUnCapacity;
            metric_Unit_Convert(&mUnCapacity, &mUnCapUnit);
            capacity_Unit_Convert(&unCapacity, &unCapUnit);
            printf("\tUnallocated NVM Capacity (%s/%s): %0.02f/%0.02f\n", mUnCapUnit, unCapUnit, mUnCapacity, unCapacity);
        }
    }
    printf("\tWrite Cache: ");
    if (driveInfo->controllerData.volatileWriteCacheSupported)
    {
        if (driveInfo->controllerData.volatileWriteCacheEnabled)
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tMaximum Number Of Namespaces: %" PRIu32 "\n", driveInfo->controllerData.maxNumberOfNamespaces);
    //Putting SMART & DST data here so that it isn't confused with the namespace data below - TJE
    if (driveInfo->smartData.valid)
    {
        printf("\tRead-Only Medium: ");
        if (driveInfo->smartData.mediumIsReadOnly)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\tSMART Status: ");
        switch (driveInfo->smartData.smartStatus)
        {
        case 0:
            printf("Good\n");
            break;
        case 1:
            printf("Bad\n");
            break;
        case 2:
        default:
            printf("Unknown\n");
            break;
        }
        //kelvin_To_Celsius(&driveInfo->smartData.compositeTemperatureKelvin);
        printf("\tComposite Temperature (K): %" PRIu16 "\n", driveInfo->smartData.compositeTemperatureKelvin);
        printf("\tPercent Used (%%): %" PRIu8 "\n", driveInfo->smartData.percentageUsed);
        printf("\tAvailable Spare (%%): %" PRIu8 "\n", driveInfo->smartData.availableSpacePercent);
        uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time_Double(driveInfo->smartData.powerOnHoursD * 3600.0, &years, &days, &hours, &minutes, &seconds);
        printf("\tPower On Time: ");
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
        printf("\n");
        printf("\tPower On Hours (hours): %0.00f\n", driveInfo->smartData.powerOnHoursD);

        //Last DST information
        printf("\tLast DST information:\n");
        if (driveInfo->dstInfo.informationValid)
        {
            if (driveInfo->smartData.powerOnHoursD - (driveInfo->dstInfo.powerOnHours) != driveInfo->smartData.powerOnHoursD)
            {
                double timeSinceLastDST = (double)((double)((double)driveInfo->smartData.powerOnHoursD) - (double)driveInfo->dstInfo.powerOnHours);
                printf("\t\tTime since last DST (hours): ");
                if (timeSinceLastDST > 0)
                {
                    printf("%0.02f\n", timeSinceLastDST);
                }
                else
                {
                    printf("Indeterminate\n");
                }
                printf("\t\tDST Status/Result: 0x%"PRIX8"\n", driveInfo->dstInfo.resultOrStatus);
                printf("\t\tDST Test run: 0x%"PRIX8"\n", driveInfo->dstInfo.testNumber);
                if (driveInfo->dstInfo.resultOrStatus != 0 && driveInfo->dstInfo.resultOrStatus != 0xF && driveInfo->dstInfo.errorLBA != UINT64_MAX)
                {
                    //Show the Error LBA
                    printf("\t\tError occurred at LBA: %"PRIu64"\n", driveInfo->dstInfo.errorLBA);
                }
            }
            else
            {
                printf("\t\tDST has never been run\n");
            }
        }
        else
        {
            printf("\t\tNot supported\n");
        }
        //Long DST time
        printf("\tLong Drive Self Test Time: ");
        if (driveInfo->controllerData.longDSTTimeMinutes > 0)
        {
            //print as hours:minutes
            uint8_t years, days = 0, hours = 0, minutes = 0, seconds = 0;
            convert_Seconds_To_Displayable_Time(driveInfo->controllerData.longDSTTimeMinutes * 60, &years, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
        }
        else
        {
            printf("Not Supported");
        }
        printf("\n");
        
        //Workload Rate (Annualized)
        printf("\tAnnualized Workload Rate (TB/yr): ");
#ifndef MINUTES_IN_1_YEAR
#define MINUTES_IN_1_YEAR 525600.0
#endif // !MINUTES_IN_1_YEAR
        double totalTerabytesRead = (double)((driveInfo->smartData.dataUnitsReadD * 512.0 * 1000.0) / 1000000000000.0);
        double totalTerabytesWritten = (double)((driveInfo->smartData.dataUnitsWrittenD * 512.0 * 1000.0) / 1000000000000.0);
        double calculatedUsage = (double)(totalTerabytesRead + totalTerabytesWritten) * (double)(MINUTES_IN_1_YEAR / (double)(driveInfo->smartData.powerOnHoursD * 60.0));
        printf("%0.02f\n", calculatedUsage);
        //Total Bytes Read
        printf("\tTotal Bytes Read ");
        double totalBytesRead = driveInfo->smartData.dataUnitsReadD * 512.0 * 1000.0;
        char unitReadString[4] = { '\0' };
        char *unitRead = &unitReadString[0];
        metric_Unit_Convert(&totalBytesRead, &unitRead);
        printf("(%s): %0.02f\n", unitRead, totalBytesRead);
        //Total Bytes Written
        printf("\tTotal Bytes Written ");
        double totalBytesWritten = driveInfo->smartData.dataUnitsWrittenD * 512.0 * 1000.0;
        char unitWrittenString[4] = { '\0' };
        char *unitWritten = &unitWrittenString[0];
        metric_Unit_Convert(&totalBytesWritten, &unitWritten);
        printf("(%s): %0.02f\n", unitWritten, totalBytesWritten);

    }
    //Encryption Support
    printf("\tEncryption Support: ");
    switch (driveInfo->controllerData.encryptionSupport)
    {
    case ENCRYPTION_SELF_ENCRYPTING:
        printf("Self Encrypting\n");
        /*if (driveInfo->trustedCommandsBeingBlocked)
        {
            printf("\t\tWARNING: OS is blocking TCG commands over passthrough. Please enable it before running any TCG commands\n");
        }*/
        break;
    case ENCRYPTION_FULL_DISK:
        printf("Full Disk Encryption\n");
        break;
    case ENCRYPTION_NONE:
    default:
        printf("Not Supported\n");
        break;
    }
    //number of firmware slots
    printf("\tNumber of Firmware Slots: %" PRIu8 "\n", driveInfo->controllerData.numberOfFirmwareSlots);
    //Print out Controller features! (admin commands, etc)
    printf("\tController Features:\n");
    for (uint16_t featureIter = 0; featureIter < driveInfo->controllerData.numberOfControllerFeatures; ++featureIter)
    {
        printf("\t\t%s\n", driveInfo->controllerData.controllerFeaturesSupported[featureIter]);
    }

    printf("\nNVMe Namespace Information:\n");
    if (driveInfo->namespaceData.valid)
    {
        //Namespace size
        char mSizeUnits[4] = { 0 }, sizeUnits[4] = { 0 };
        char *mSizeUnit = &mSizeUnits[0], *sizeUnit = &sizeUnits[0];
        double nvmMSize = (double)(driveInfo->namespaceData.namespaceSize * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmSize = nvmMSize;
        metric_Unit_Convert(&nvmMSize, &mSizeUnit);
        capacity_Unit_Convert(&nvmSize, &sizeUnit);
        printf("\tNamespace Size (%s/%s): %0.02f/%0.02f\n", mSizeUnit, sizeUnit, nvmMSize, nvmSize);
        printf("\tNamespace Size (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceSize);

        //namespace capacity
        char mCapUnits[4] = { 0 }, capUnits[4] = { 0 };
        char *mCapUnit = &mCapUnits[0], *capUnit = &capUnits[0];
        double nvmMCap = (double)(driveInfo->namespaceData.namespaceCapacity * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmCap = nvmMCap;
        metric_Unit_Convert(&nvmMCap, &mCapUnit);
        capacity_Unit_Convert(&nvmCap, &capUnit);
        printf("\tNamespace Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, nvmMCap, nvmCap);
        printf("\tNamespace Capacity (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceCapacity);

        //namespace utilization
        char mUtilizationUnits[4] = { 0 }, utilizationUnits[4] = { 0 };
        char *mUtilizationUnit = &mUtilizationUnits[0], *utilizationUnit = &utilizationUnits[0];
        double nvmMUtilization = (double)(driveInfo->namespaceData.namespaceUtilization * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmUtilization = nvmMUtilization;
        metric_Unit_Convert(&nvmMUtilization, &mUtilizationUnit);
        capacity_Unit_Convert(&nvmUtilization, &utilizationUnit);
        printf("\tNamespace Utilization (%s/%s): %0.02f/%0.02f\n", mUtilizationUnit, utilizationUnit, nvmMUtilization, nvmUtilization);
        printf("\tNamespace Utilization (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceUtilization);

        //Formatted LBA Size
        printf("\tLogical Block Size (B): %" PRIu32 "\n", driveInfo->namespaceData.formattedLBASizeBytes);

        //relative performance
        printf("\tLogical Block Size Relative Performance: ");
        switch (driveInfo->namespaceData.relativeFormatPerformance)
        {
        case 0:
            printf("Best Performance\n");
            break;
        case 1:
            printf("Better Performance\n");
            break;
        case 2:
            printf("Good Performance\n");
            break;
        case 3:
            printf("Degraded Performance\n");
            break;
        default://this case shouldn't ever happen...just reducing a warning - TJE
            printf("Unknown Performance\n");
            break;
        }
        if (driveInfo->namespaceData.nvmCapacityD > 0)
        {
            char mCapUnits[4] = { 0 }, capUnits[4] = { 0 };
            char *mCapUnit = &mCapUnits[0], *capUnit = &capUnits[0];
            double mCapacity = driveInfo->namespaceData.nvmCapacityD;
            double capacity = mCapacity;
            metric_Unit_Convert(&mCapacity, &mCapUnit);
            capacity_Unit_Convert(&capacity, &capUnit);
            printf("\tNVM Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
        }
        printf("\tNGUID: ");
        if (memcmp(zero128Bit, driveInfo->namespaceData.namespaceGloballyUniqueIdentifier, 16))
        {
            for (uint8_t i = 0; i < 16; ++i)
            {
                printf("%02" PRIX8, driveInfo->controllerData.fguid[i]);
            }
            printf("\n");
        }
        else
        {
            printf("Not Supported\n");
        }
        printf("\tEUI64: ");
        if (driveInfo->namespaceData.ieeeExtendedUniqueIdentifier != 0)
        {
            printf("%016" PRIX64 "\n", driveInfo->namespaceData.ieeeExtendedUniqueIdentifier);
        }
        else
        {
            printf("Not Supported\n");
        }
        //Namespace features.
        printf("\tNamespace Features:\n");
        for (uint16_t featureIter = 0; featureIter < driveInfo->namespaceData.numberOfNamespaceFeatures; ++featureIter)
        {
            printf("\t\t%s\n", driveInfo->namespaceData.namespaceFeaturesSupported[featureIter]);
        }
    }
    else
    {
        printf("\tERROR: Could not get namespace data!\n");
    }
    printf("\n");
}

void print_SAS_Sata_Device_Information(ptrDriveInformationSAS_SATA driveInfo)
{
    double mCapacity = 0, capacity = 0;
    char mCapUnits[4] = { 0 }, capUnits[4] = { 0 };
    char *mCapUnit = &mCapUnits[0], *capUnit = &capUnits[0];
    if (strlen(driveInfo->vendorID))
    {
        printf("\tVendor ID: %s\n", driveInfo->vendorID);
    }
    printf("\tModel Number: %s\n", driveInfo->modelNumber);
    printf("\tSerial Number: %s\n", driveInfo->serialNumber);
    printf("\tFirmware Revision: %s\n", driveInfo->firmwareRevision);
    if (strlen(driveInfo->satVendorID))
    {
        printf("\tSAT Vendor ID: %s\n", driveInfo->satVendorID);
    }
    if (strlen(driveInfo->satProductID))
    {
        printf("\tSAT Product ID: %s\n", driveInfo->satProductID);
    }
    if (strlen(driveInfo->satProductRevision))
    {
        printf("\tSAT Product Rev: %s\n", driveInfo->satProductRevision);
    }
    printf("\tWorld Wide Name: ");
    if (driveInfo->worldWideNameSupported)
    {
        printf("%016" PRIX64 "", driveInfo->worldWideName);
        if (driveInfo->worldWideNameExtensionValid)
        {
            printf("%016" PRIX64 "", driveInfo->worldWideNameExtension);
        }
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    if (driveInfo->copyrightValid && strlen(driveInfo->copyrightInfo))
    {
        printf("\tCopyright: %s\n", driveInfo->copyrightInfo);
    }
    //Drive capacity
    mCapacity = (double)(driveInfo->maxLBA * driveInfo->logicalSectorSize);
    if (driveInfo->maxLBA == 0 && driveInfo->ataLegacyCHSInfo.legacyCHSValid)
    {
        if (driveInfo->ataLegacyCHSInfo.currentCapacityInSectors > 0)
        {
            mCapacity = (double)(driveInfo->ataLegacyCHSInfo.currentCapacityInSectors * driveInfo->logicalSectorSize);
        }
        else
        {
            mCapacity = (double)((driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders * driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads * driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack) * driveInfo->logicalSectorSize);
        }
    }
    capacity = mCapacity;
    metric_Unit_Convert(&mCapacity, &mCapUnit);
    capacity_Unit_Convert(&capacity, &capUnit);
    printf("\tDrive Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
    if (!(driveInfo->nativeMaxLBA == 0 || driveInfo->nativeMaxLBA == UINT64_MAX))
    {
        mCapacity = (double)(driveInfo->nativeMaxLBA * driveInfo->logicalSectorSize);
        capacity = mCapacity;
        metric_Unit_Convert(&mCapacity, &mCapUnit);
        capacity_Unit_Convert(&capacity, &capUnit);
        printf("\tNative Drive Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
    }
    printf("\tTemperature Data:\n");
    if (driveInfo->temperatureData.temperatureDataValid)
    {
        printf("\t\tCurrent Temperature (C): %"PRId16"\n", driveInfo->temperatureData.currentTemperature);
    }
    else
    {
        printf("\t\tCurrent Temperature (C): Not Reported\n");
    }
    //Highest Temperature
    if (driveInfo->temperatureData.highestValid)
    {
        printf("\t\tHighest Temperature (C): %"PRId16"\n", driveInfo->temperatureData.highestTemperature);
    }
    else
    {
        printf("\t\tHighest Temperature (C): Not Reported\n");
    }
    //Lowest Temperature
    if (driveInfo->temperatureData.lowestValid)
    {
        printf("\t\tLowest Temperature (C): %"PRId16"\n", driveInfo->temperatureData.lowestTemperature);
    }
    else
    {
        printf("\t\tLowest Temperature (C): Not Reported\n");
    }
    if (driveInfo->humidityData.humidityDataValid)
    {
        //Humidity Data
        printf("\tHumidity Data:\n");
        if (driveInfo->humidityData.humidityDataValid)
        {
            printf("\t\tCurrent Humidity (%%): %"PRIu8"\n", driveInfo->humidityData.currentHumidity);
        }
        else
        {
            printf("\t\tCurrent Humidity (%%): Not Reported\n");
        }
        if (driveInfo->humidityData.highestValid)
        {
            printf("\t\tHighest Humidity (%%): %"PRIu8"\n", driveInfo->humidityData.highestHumidity);
        }
        else
        {
            printf("\t\tHighest Humidity (%%): Not Reported\n");
        }
        if (driveInfo->humidityData.lowestValid)
        {
            printf("\t\tLowest Humidity (%%): %"PRIu8"\n", driveInfo->humidityData.lowestHumidity);
        }
        else
        {
            printf("\t\tLowest Humidity (%%): Not Reported\n");
        }
    }
    //Power On Time
    printf("\tPower On Time: ");
    if (driveInfo->powerOnMinutes > 0)
    {
        uint8_t years, days = 0, hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time(driveInfo->powerOnMinutes * 60, &years, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
    }
    else
    {
        printf("Not Reported");
    }
    printf("\n");
    printf("\tPower On Hours: ");
    if (driveInfo->powerOnMinutes > 0)
    {
        //convert to a double to display as xx.xx
        double powerOnHours = (double)((double)driveInfo->powerOnMinutes / 60.00);
        printf("%0.02f", powerOnHours);
    }
    else
    {
        printf("Not Reported");
    }
    printf("\n");
    if (driveInfo->ataLegacyCHSInfo.legacyCHSValid && driveInfo->maxLBA == 0)
    {
        printf("\tDefault CHS: %" PRIu16 " | %" PRIu8 " | %" PRIu8 "\n", driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders, driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads, driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack);
        printf("\tCurrent CHS: %" PRIu16 " | %" PRIu8 " | %" PRIu8 "\n", driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack);
        uint32_t simMaxLBA = 0;
        if (driveInfo->ataLegacyCHSInfo.currentInfoconfigurationValid)
        {
            simMaxLBA = driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders * driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads * driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack;
        }
        else
        {
            simMaxLBA = driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders * driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads * driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack;
        }
        printf("\tSimulated MaxLBA: %" PRIu32 "\n", simMaxLBA);
    }
    else
    {
        //MaxLBA
        printf("\tMaxLBA: %"PRIu64"\n", driveInfo->maxLBA);
        //Native Max LBA
        printf("\tNative MaxLBA: ");
        if (driveInfo->nativeMaxLBA == 0 || driveInfo->nativeMaxLBA == UINT64_MAX)
        {
            printf("Not Reported\n");
        }
        else
        {
            printf("%"PRIu64"\n", driveInfo->nativeMaxLBA);
        }
    }
    if (driveInfo->isFormatCorrupt)
    {
        //Logical Sector Size
        printf("\tLogical Sector Size (B): Format Corrupt\n");
        //Physical Sector Size
        printf("\tPhysical Sector Size (B): Format Corrupt\n");
        //Sector Alignment
        printf("\tSector Alignment: Format Corrupt\n");
    }
    else
    {
        //Logical Sector Size
        printf("\tLogical Sector Size (B): %"PRIu32"\n", driveInfo->logicalSectorSize);
        //Physical Sector Size
        printf("\tPhysical Sector Size (B): %"PRIu32"\n", driveInfo->physicalSectorSize);
        //Sector Alignment
        printf("\tSector Alignment: %"PRIu16"\n", driveInfo->sectorAlignment);
    }
    //Rotation Rate
    printf("\tRotation Rate (RPM): ");
    if (driveInfo->rotationRate == 0)
    {
        printf("Not Reported\n");
    }
    else if (driveInfo->rotationRate == 0x0001)
    {
        printf("SSD\n");
    }
    else
    {
        printf("%"PRIu16"\n", driveInfo->rotationRate);
    }
    if (driveInfo->isWriteProtected)
    {
        printf("\tMedium is write protected!\n");
    }
    //Form Factor
    printf("\tForm Factor: ");
    switch (driveInfo->formFactor)
    {
    case 1:
        printf("5.25\"\n");
        break;
    case 2:
        printf("3.5\"\n");
        break;
    case 3:
        printf("2.5\"\n");
        break;
    case 4:
        printf("1.8\"\n");
        break;
    case 5:
        printf("Less than 1.8\"\n");
        break;
    case 6:
        printf("mSATA\n");
        break;
    case 7:
        printf("M.2\n");
        break;
    case 8:
        printf("MicroSSD\n");
        break;
    case 9:
        printf("CFast\n");
        break;
    case 0:
    default:
        printf("Not Reported\n");
        break;
    }
    //Last DST information
    printf("\tLast DST information:\n");
    if (driveInfo->dstInfo.informationValid)
    {
        if (driveInfo->powerOnMinutes - (driveInfo->dstInfo.powerOnHours * 60) != driveInfo->powerOnMinutes)
        {
            double timeSinceLastDST = (double)((double)((double)driveInfo->powerOnMinutes / 60.0) - (double)driveInfo->dstInfo.powerOnHours);
            printf("\t\tTime since last DST (hours): ");
            if (timeSinceLastDST > 0)
            {
                printf("%0.02f\n", timeSinceLastDST);
            }
            else
            {
                printf("Indeterminate\n");
            }
            printf("\t\tDST Status/Result: 0x%"PRIX8"\n", driveInfo->dstInfo.resultOrStatus);
            printf("\t\tDST Test run: 0x%"PRIX8"\n", driveInfo->dstInfo.testNumber);
            if (driveInfo->dstInfo.resultOrStatus != 0 && driveInfo->dstInfo.resultOrStatus != 0xF && driveInfo->dstInfo.errorLBA != UINT64_MAX)
            {
                //Show the Error LBA
                printf("\t\tError occurred at LBA: %"PRIu64"\n", driveInfo->dstInfo.errorLBA);
            }
        }
        else
        {
            printf("\t\tDST has never been run\n");
        }
    }
    else
    {
        printf("\t\tNot supported\n");
    }
    //Long DST time
    printf("\tLong Drive Self Test Time: ");
    if (driveInfo->longDSTTimeMinutes > 0)
    {
        //print as hours:minutes
        uint8_t years, days = 0, hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time(driveInfo->longDSTTimeMinutes * 60, &years, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    //Interface Speed
    printf("\tInterface speed:\n");
    if (driveInfo->interfaceSpeedInfo.speedIsValid)
    {
        if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_SERIAL)
        {
            if (driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts > 0)
            {
                if (driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts == 1)
                {
                    printf("\t\tMax Speed (Gb/s): ");
                    switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0])
                    {
                    case 5:
                        printf("22.5");
                        break;
                    case 4:
                        printf("12.0");
                        break;
                    case 3:
                        printf("6.0");
                        break;
                    case 2:
                        printf("3.0");
                        break;
                    case 1:
                        printf("1.5");
                        break;
                    case 0:
                        printf("Not Reported");
                        break;
                    default:
                        printf("Unknown");
                        break;
                    }
                    printf("\n");
                    printf("\t\tNegotiated Speed (Gb/s): ");
                    switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0])
                    {
                    case 5:
                        printf("22.5");
                        break;
                    case 4:
                        printf("12.0");
                        break;
                    case 3:
                        printf("6.0");
                        break;
                    case 2:
                        printf("3.0");
                        break;
                    case 1:
                        printf("1.5");
                        break;
                    case 0:
                        printf("Not Reported");
                        break;
                    default:
                        printf("Unknown");
                        break;
                    }
                    printf("\n");
                }
                else
                {
                    for (uint8_t portIter = 0; portIter < driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts && portIter < MAX_PORTS; portIter++)
                    {
                        printf("\t\tPort %"PRIu8"", portIter);
                        if (driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber == portIter && driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber != UINT8_MAX)
                        {
                            printf(" (Current Port)");
                        }
                        printf("\n");
                        //Max Speed
                        printf("\t\t\tMax Speed (GB/s): ");
                        switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[portIter])
                        {
                        case 5:
                            printf("22.5");
                            break;
                        case 4:
                            printf("12.0");
                            break;
                        case 3:
                            printf("6.0");
                            break;
                        case 2:
                            printf("3.0");
                            break;
                        case 1:
                            printf("1.5");
                            break;
                        case 0:
                            printf("Not Reported");
                            break;
                        default:
                            printf("Unknown");
                            break;
                        }
                        printf("\n");
                        //Negotiated speed
                        printf("\t\t\tNegotiated Speed (Gb/s): ");
                        switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[portIter])
                        {
                        case 5:
                            printf("22.5");
                            break;
                        case 4:
                            printf("12.0");
                            break;
                        case 3:
                            printf("6.0");
                            break;
                        case 2:
                            printf("3.0");
                            break;
                        case 1:
                            printf("1.5");
                            break;
                        case 0:
                            printf("Not Reported");
                            break;
                        default:
                            printf("Unknown");
                            break;
                        }
                        printf("\n");
                    }
                }
            }
            else
            {
                printf("\t\tNot Reported\n");
            }
        }
        else if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_PARALLEL)
        {
            printf("\t\tMax Speed (MB/s): %0.02f", driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed);
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid)
            {
                printf(" (%s)", driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName);
            }
            printf("\n");
            printf("\t\tNegotiated Speed (MB/s): ");
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid)
            {
                printf("%0.02f", driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed);
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid)
                {
                    printf(" (%s)", driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName);
                }
                printf("\n");
            }
            else
            {
                printf("Not Reported\n");
            }
        }
        else if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_ANCIENT)
        {
            if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt10MbS)
            {
                printf("\t\t>10Mb/s\n");
            }
            else if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt5MbSLte10MbS)
            {
                printf("\t\t>5Mb/s & <10Mb/s\n");
            }
            else if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferLte5MbS)
            {
                printf("\t\t<5Mb/s\n");
            }
            else
            {
                printf("\t\tNot Reported\n");
            }
        }
        else
        {
            printf("\t\tNot Reported\n");
        }
    }
    else
    {
        printf("\t\tNot Reported\n");
    }
    //Workload Rate (Annualized)
    printf("\tAnnualized Workload Rate (TB/yr): ");
    if (driveInfo->totalBytesRead > 0 || driveInfo->totalBytesWritten > 0)
    {
        if (driveInfo->powerOnMinutes > 0)
        {
#ifndef MINUTES_IN_1_YEAR
#define MINUTES_IN_1_YEAR 525600.0
#endif // !MINUTES_IN_1_YEAR
            double totalTerabytesRead = (double)(driveInfo->totalBytesRead / 1000000000000.0);
            double totalTerabytesWritten = (double)(driveInfo->totalBytesWritten / 1000000000000.0);
            double calculatedUsage = (double)(totalTerabytesRead + totalTerabytesWritten) * (double)(MINUTES_IN_1_YEAR / (double)driveInfo->powerOnMinutes);
            printf("%0.02f\n", calculatedUsage);
        }
        else
        {
            printf("0.00\n");
        }
    }
    else
    {
        printf("Not Reported\n");
    }
    //Total Bytes Read
    printf("\tTotal Bytes Read ");
    if (driveInfo->totalBytesRead > 0)
    {
        double totalBytesRead = (double)driveInfo->totalBytesRead;
        char unitString[4] = { '\0' };
        char *unit = &unitString[0];
        metric_Unit_Convert(&totalBytesRead, &unit);
        printf("(%s): %0.02f\n", unit, totalBytesRead);
    }
    else
    {
        printf("(B): Not Reported\n");
    }
    //Total Bytes Written
    printf("\tTotal Bytes Written ");
    if (driveInfo->totalBytesWritten > 0)
    {
        double totalBytesWritten = (double)driveInfo->totalBytesWritten;
        char unitString[4] = { '\0' };
        char *unit = &unitString[0];
        metric_Unit_Convert(&totalBytesWritten, &unit);
        printf("(%s): %0.02f\n", unit, totalBytesWritten);
    }
    else
    {
        printf("(B): Not Reported\n");
    }
    //Drive reported Utilization
    if (driveInfo->deviceReportedUtilizationRate > 0)
    {
        printf("\tDrive Reported Utilization (%%): ");
        printf("%0.04f", driveInfo->deviceReportedUtilizationRate);
    }
    //Encryption Support
    printf("\tEncryption Support: ");
    switch (driveInfo->encryptionSupport)
    {
    case ENCRYPTION_SELF_ENCRYPTING:
        printf("Self Encrypting\n");
        if (driveInfo->trustedCommandsBeingBlocked)
        {
            printf("\t\tWARNING: OS is blocking TCG commands over passthrough. Please enable it before running any TCG commands\n");
        }
        break;
    case ENCRYPTION_FULL_DISK:
        printf("Full Disk Encryption\n");
        break;
    case ENCRYPTION_NONE:
    default:
        printf("Not Supported\n");
        break;
    }
    //Cache Size -- convert to MB
    if (driveInfo->cacheSize > 0)
    {
        double cacheSize = (double)driveInfo->cacheSize;
        char cacheUnit[4] = { 0 };
        char *cachUnitPtr = &cacheUnit[0];
        capacity_Unit_Convert(&cacheSize, &cachUnitPtr);
        printf("\tCache Size (%s): %0.02f\n", cacheUnit, cacheSize);
    }
    else
    {
        printf("\tCache Size (MiB): Not Reported\n");
    }
    //Hybrid NAND Cache Size -- convert to GB
    if (driveInfo->hybridNANDSize > 0)
    {
        double cacheSize = (double)driveInfo->hybridNANDSize;
        char cacheUnit[4] = { 0 };
        char *cachUnitPtr = &cacheUnit[0];
        capacity_Unit_Convert(&cacheSize, &cachUnitPtr);
        printf("\tHybrid NAND Cache Size (%s): %0.02f\n", cacheUnit, cacheSize);
    }
    //Percent Endurance Used
    if (driveInfo->rotationRate == 0x0001)
    {
        if (driveInfo->percentEnduranceUsed >= 0)
        {
            printf("\tPercentage Used Endurance Indicator (%%): %0.05f\n", driveInfo->percentEnduranceUsed);
        }
        else
        {
            printf("\tPercentage Used Endurance Indicator (%%): Not Reported\n");
        }
    }
    //Write Amplification
    if (driveInfo->rotationRate == 0x0001 && driveInfo->totalWritesToFlash > 0)
    {
        if (driveInfo->totalLBAsWritten > 0)
        {
            printf("\tWrite Amplification (%%): %0.02f\n", (double)driveInfo->totalWritesToFlash / (double)driveInfo->totalLBAsWritten);
        }
        else
        {
            printf("\tWrite Amplification (%%): 0\n");
        }
    }
    //Read look ahead
    if (driveInfo->readLookAheadSupported)
    {
        if (driveInfo->readLookAheadEnabled)
        {
            printf("\tRead Look-Ahead: Enabled\n");
        }
        else
        {
            printf("\tRead Look-Ahead: Disabled\n");
        }
    }
    else
    {
        printf("\tRead Look-Ahead: Not Supported\n");
    }
    //NVCache (!NV_DIS bit from caching MP)
    if (driveInfo->nvCacheSupported)
    {
        printf("\tNon-Volatile Cache: ");
        if (driveInfo->nvCacheEnabled)
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
    }
    //Write Cache
    if (driveInfo->writeCacheSupported)
    {
        if (driveInfo->writeCacheEnabled)
        {
            printf("\tWrite Cache: Enabled\n");
        }
        else
        {
            printf("\tWrite Cache: Disabled\n");
        }
    }
    else
    {
        printf("\tWrite Cache: Not Supported\n");
    }
    if (driveInfo->lowCurrentSpinupValid)
    {
        if (driveInfo->lowCurrentSpinupViaSCT)//to handle differences in reporting between 2.5" products and others
        {
            printf("\tLow Current Spinup: ");
            switch (driveInfo->lowCurrentSpinupEnabled)
            {
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_LOW:
                printf("Enabled\n");
                break;
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_DEFAULT:
                printf("Disabled\n");
                break;
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_ULTRA_LOW:
                printf("Ultra Low Enabled\n");
                break;
            default:
                printf("Unknown/Invalid state: %" PRIX16 "\n", (uint16_t)driveInfo->lowCurrentSpinupEnabled);
                break;
            }
        }
        else
        {
            if (driveInfo->lowCurrentSpinupEnabled > 0)
            {
                printf("\tLow Current Spinup: Enabled\n");
            }
            else
            {
                printf("\tLow Current Spinup: Disabled\n");
            }
        }
    }
    //SMART Status
    printf("\tSMART Status: ");
    switch (driveInfo->smartStatus)
    {
    case 0://good
        printf("Good\n");
        break;
    case 1://bad
        printf("Tripped\n");
        break;
    default://unknown
        printf("Unknown or Not Supported\n");
        break;
    }
    //ATA Security Infomation
    printf("\tATA Security Information: ");
    if (driveInfo->ataSecurityInformation.securitySupported)
    {
        printf("Supported");
        if (driveInfo->ataSecurityInformation.securityEnabled)
        {
            printf(", Enabled");
        }
        if (driveInfo->ataSecurityInformation.securityLocked)
        {
            printf(", Locked");
        }
        if (driveInfo->ataSecurityInformation.securityFrozen)
        {
            printf(", Frozen");
        }
        if (driveInfo->ataSecurityInformation.securityCountExpired)
        {
            printf(", Password Count Expired");
        }
        printf("\n");
    }
    else
    {
        printf("Not Supported\n");
    }
    //Zoned Device Type
    if (driveInfo->zonedDevice > 0)
    {
        printf("\tZoned Device Type: ");
        switch (driveInfo->zonedDevice)
        {
        case 0x1://host aware
            printf("Host Aware\n");
            break;
        case 0x2://host managed
            printf("Device Managed\n");
            break;
        case 0x3://reserved
            printf("Reserved\n");
            break;
        default:
            printf("Not a Zoned Device\n");
            break;
        }
    }
    printf("\tFirmware Download Support: ");
    if (driveInfo->fwdlSupport.downloadSupported)
    {
        printf("Full");//changed to "Full" from "Immediate" since this makes more sense...-TJE
        if (driveInfo->fwdlSupport.segmentedSupported)
        {
            printf(", Segmented");
            if (driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired)
            {
                printf(" as Deferred - Power Cycle Activation Only");
            }
        }
        if (driveInfo->fwdlSupport.deferredSupported)
        {
            printf(", Deferred");
        }
        if (driveInfo->fwdlSupport.dmaModeSupported)
        {
            printf(", DMA");
        }
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    //Specifications Supported
    printf("\tSpecifications Supported:\n");
    if (driveInfo->numberOfSpecificationsSupported > 0)
    {
        uint8_t specificationsIter = 0;
        for (specificationsIter = 0; specificationsIter < driveInfo->numberOfSpecificationsSupported && specificationsIter < MAX_SPECS; specificationsIter++)
        {
            printf("\t\t%s\n", driveInfo->specificationsSupported[specificationsIter]);
        }
    }
    else
    {
        printf("\t\tNone reported by device.\n");
    }
    //Features Supported
    printf("\tFeatures Supported:\n");
    if (driveInfo->numberOfFeaturesSupported > 0)
    {
        uint8_t featuresIter = 0;
        for (featuresIter = 0; featuresIter < driveInfo->numberOfFeaturesSupported && featuresIter < MAX_FEATURES; featuresIter++)
        {
            printf("\t\t%s\n", driveInfo->featuresSupported[featuresIter]);
        }
    }
    else
    {
        printf("\t\tNone reported or an error occurred while trying to determine\n\t\tthe features.\n");
    }
    return;
}

//This exists so we can print out SCSI reported and ATA reported information for comparison purposes. (SAT test/check) NOT FOR USE WITH A SAS DRIVE
void print_Parent_And_Child_Information(ptrDriveInformation translatorDriveInfo, ptrDriveInformation driveInfo)
{
    if (translatorDriveInfo && translatorDriveInfo->infoType == DRIVE_INFO_SAS_SATA)
    {
        printf("SCSI Translator Reported Information:\n");
        print_Device_Information(translatorDriveInfo);
    }
    else
    {
        printf("SCSI Translator Information Not Available.\n\n");
    }
    if (driveInfo && driveInfo->infoType == DRIVE_INFO_SAS_SATA)
    {
        printf("ATA Reported Information:\n");
        print_Device_Information(driveInfo);
    }
    else if (driveInfo && driveInfo->infoType == DRIVE_INFO_NVME)
    {
        printf("NVMe Reported Information:\n");
        print_Device_Information(driveInfo);
    }
    else if(driveInfo)
    {
        printf("Unknown device Information type:\n");
        print_Device_Information(driveInfo);
    }
    else
    {
        printf("Drive Information not available.\n\n");
    }
}

//This function ONLY exists because we need to show a mix of SCSI and ATA information on USB.
void generate_External_Drive_Information(ptrDriveInformationSAS_SATA externalDriveInfo, ptrDriveInformationSAS_SATA scsiDriveInfo, ptrDriveInformationSAS_SATA ataDriveInfo)
{
    if (externalDriveInfo && scsiDriveInfo && ataDriveInfo)
    {
        //take data from each of the inputs, and plug it into a new one, then call the standard print function
        memcpy(externalDriveInfo, ataDriveInfo, sizeof(driveInformation));
        //we have a copy of the ata info, now just change the stuff we want to show from scsi info
        memset(externalDriveInfo->vendorID, 0, 8);
        memcpy(externalDriveInfo->vendorID, scsiDriveInfo->vendorID, 8);
        memset(externalDriveInfo->modelNumber, 0, 40);
        memcpy(externalDriveInfo->modelNumber, scsiDriveInfo->modelNumber, strlen(scsiDriveInfo->modelNumber));
        memset(externalDriveInfo->serialNumber, 0, 20);
        memcpy(externalDriveInfo->serialNumber, scsiDriveInfo->serialNumber, strlen(scsiDriveInfo->serialNumber));
        memset(externalDriveInfo->firmwareRevision, 0, 10);
        memcpy(externalDriveInfo->firmwareRevision, scsiDriveInfo->firmwareRevision, strlen(scsiDriveInfo->firmwareRevision));
        externalDriveInfo->maxLBA = scsiDriveInfo->maxLBA;
        externalDriveInfo->nativeMaxLBA = scsiDriveInfo->nativeMaxLBA;
        externalDriveInfo->logicalSectorSize = scsiDriveInfo->logicalSectorSize;
        externalDriveInfo->physicalSectorSize = scsiDriveInfo->physicalSectorSize;
        externalDriveInfo->sectorAlignment = scsiDriveInfo->sectorAlignment;
        externalDriveInfo->zonedDevice = scsiDriveInfo->zonedDevice;

        //copy specifications supported into the external drive info.
        uint16_t extSpecNumber = externalDriveInfo->numberOfSpecificationsSupported;
        uint16_t scsiSpecNumber = 0;
        for (; extSpecNumber < MAX_SPECS && scsiSpecNumber < scsiDriveInfo->numberOfSpecificationsSupported; ++extSpecNumber, ++scsiSpecNumber)
        {
            memcpy(&externalDriveInfo->specificationsSupported[extSpecNumber], &scsiDriveInfo->specificationsSupported[scsiSpecNumber], MAX_SPEC_LENGTH);
            ++(externalDriveInfo->numberOfSpecificationsSupported);
        }

    }
    return;
}

int print_Drive_Information(tDevice *device, bool showChildInformation)
{
    int ret = SUCCESS;
    ptrDriveInformation ataDriveInfo = NULL, scsiDriveInfo = NULL, usbDriveInfo = NULL, nvmeDriveInfo = NULL;
    //Always allocate scsiDrive info since it will always be available no matter the drive type we are talking to!
    scsiDriveInfo = (ptrDriveInformation)calloc(sizeof(driveInformation), sizeof(driveInformation));
    scsiDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //allocate ataDriveInfo since this is an ATA drive
        ataDriveInfo = (ptrDriveInformation)calloc(sizeof(driveInformation), sizeof(driveInformation));
        ataDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
        ret = get_ATA_Drive_Information(device, &ataDriveInfo->sasSata);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //allocate nvmeDriveInfo since this is an NVMe drive
        nvmeDriveInfo = (ptrDriveInformation)calloc(sizeof(driveInformation), sizeof(driveInformation));
        nvmeDriveInfo->infoType = DRIVE_INFO_NVME;
        get_NVMe_Drive_Information(device, &nvmeDriveInfo->nvme);
    }
#endif
    //not that we have software sat, always get the scsi data.
    ret = get_SCSI_Drive_Information(device, &scsiDriveInfo->sasSata);

    if (ret == SUCCESS)
    {
        //call the print functions appropriately
        if (showChildInformation && device->drive_info.drive_type != SCSI_DRIVE)
        {
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                print_Parent_And_Child_Information(scsiDriveInfo, ataDriveInfo);
            }
            else if (device->drive_info.drive_type == NVME_DRIVE)
            {
                print_Parent_And_Child_Information(scsiDriveInfo, nvmeDriveInfo);
            }
        }
        else
        {
            //ONLY call the external function when we are able to get some passthrough information back as well
            if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE) && device->drive_info.drive_type == ATA_DRIVE)
            {
                usbDriveInfo = (ptrDriveInformation)calloc(sizeof(driveInformation), sizeof(driveInformation));
                usbDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
                generate_External_Drive_Information(&usbDriveInfo->sasSata, &scsiDriveInfo->sasSata, &ataDriveInfo->sasSata);
                print_Device_Information(usbDriveInfo);
            }
            else//ata or scsi
            {
                if (device->drive_info.drive_type == ATA_DRIVE && ataDriveInfo)
                {
                    print_Device_Information(ataDriveInfo);
                }
#if !defined(DISABLE_NVME_PASSTHROUGH)
                else if (device->drive_info.drive_type == NVME_DRIVE && nvmeDriveInfo)
                {
                    print_Device_Information(nvmeDriveInfo);
                    //print_Nvme_Ctrl_Information(device);
                }
#endif
                else if(scsiDriveInfo)
                {
                    print_Device_Information(scsiDriveInfo);
                }
                else
                {
                    printf("Error allocating memory to get device information.\n");
                }
            }
        }
    }
    safe_Free(ataDriveInfo);
    safe_Free(scsiDriveInfo);
    safe_Free(usbDriveInfo);
    safe_Free(nvmeDriveInfo);
    return ret;
}

char * print_drive_type(tDevice *device)
{
    if (device != NULL)
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            return "ATA";
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            return "SCSI"; 
        }
        else if (device->drive_info.drive_type == NVME_DRIVE)
        {
            return "NVMe";
        }
        else if (device->drive_info.drive_type == RAID_DRIVE)
        {
            return "RAID";
        }
        else if (device->drive_info.drive_type == ATAPI_DRIVE)
        {
            return "ATAPI";
        }
        else if (device->drive_info.drive_type == FLASH_DRIVE)
        {
            return "FLASH";
        }
        else if (device->drive_info.drive_type == LEGACY_TAPE_DRIVE)
        {
            return "TAPE";
        }
        else
        {
            return "UNKNOWN";
        }
    }
    else
    {
        return "NULL";
    }
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
#if 0
int print_Nvme_Ctrl_Information(tDevice *device)
{
    int ret = UNKNOWN;
    uint32_t c=0; 
    nvmeIDCtrl * ctrlData = &device->drive_info.IdentifyData.nvme.ctrl; //Conroller information data structure
    nvmeIDNameSpaces * nsData = &device->drive_info.IdentifyData.nvme.ns; //Name Space Data structure 
    char * metricUnit = NULL;
    char * capacityUnit = NULL;
    double metricCapacity = (double)device->drive_info.deviceMaxLba * device->drive_info.deviceBlockSize;
    double capacityCapacity = metricCapacity;

    printf("\tPCI Vendor ID: %"PRIX16"\n",ctrlData->vid);
    printf("\tPCI Subsystem Vendor ID: %"PRIX16"\n",ctrlData->ssvid);
    printf("\tModel Number: %s\n", device->drive_info.product_identification);
    printf("\tSerial Number: %s\n", device->drive_info.serialNumber);
    printf("\tFirmware Revision: %s\n", device->drive_info.product_revision);
    //printf("\tIEEE OUI Identifier: %X\n", ctrlData.ieee);
    /*if (ctrlData->cmic & BIT0) 
    {
        printf("\tNVM subsystem contains more than one PCI Express ports")
    }
    else
    {
        printf("\tNVM subsystem contains one PCI Express ports")
    }*/
    printf("\tController ID: %"PRIX16"\n",ctrlData->cntlid);
    if (ctrlData->oacs & BIT0) 
    {
        printf("\tController supports Security Commands\n");
    }
    if (ctrlData->oacs & BIT1) 
    {
        printf("\tController supports Format NVM Commands\n");
    }
    if (ctrlData->oacs & BIT2) 
    {
        printf("\tController supports Firmware Download & Active Commands\n");
    }
    if (ctrlData->lpa & BIT0) 
    {
        printf("\tController supports SMART/Health Information Log\n");
    }
    printf("\tError Log Page Entries: %"PRIu8"\n",ctrlData->elpe);
    printf("\tNumber of Power States Supported: %"PRIu8"\n",ctrlData->npss+1);//0 based value
    printf("\tNumber of Namespaces: %"PRIu32"\n",ctrlData->nn);

    if (ctrlData->fna)
    {
        if (ctrlData->fna & BIT0) 
        {
            printf("\tFORMAT: applies to all namespaces\n");
        }
        else
        {
            printf("\tFORMAT: processed on namespace basis\n");
        }
        if (ctrlData->fna & BIT2)
        {
            printf("\tFORMAT: Ctryptographic Erase Supported\n");
        }
    }
    if (ctrlData->vwc & BIT0) 
    {
        printf("\tController supports Volatile Write Cache\n");
    }
    printf("\tMaxLBA: %"PRIu64"\n", device->drive_info.deviceMaxLba);
    printf("\tLogical Sector Size (Bytes): %"PRIu32"\n", device->drive_info.deviceBlockSize);
    metricUnit = (char*)calloc(10, sizeof(uint8_t));
    capacityUnit = (char*)calloc(10, sizeof(uint8_t));
    metric_Unit_Convert(&metricCapacity, &metricUnit);
    capacity_Unit_Convert(&capacityCapacity,&capacityUnit);
    printf("\tCapacity (%s/%s): %.02f/%.02f\n", metricUnit, capacityUnit, metricCapacity, capacityCapacity);
    safe_Free(metricUnit);
    safe_Free(capacityUnit);

    printf("\t\t---------NAMESPACE DATA---------\n");
    printf("\t\tNamespace Size: %"PRIu64"\n",nsData->nsze);
    printf("\t\tNamespace Capacity: %"PRIu64"\n",nsData->ncap);
    printf("\t\tNamespace Used: %"PRIu64"\n", nsData->nuse);
    if (nsData->nsfeat & BIT0) 
    {
        printf("\t\tNamespace supports thin provisioning\n");
    }
    if (nsData->mc)
    {
        if (nsData->mc & BIT0)
        {
            printf("\t\tNamespace supports metadata as part of extended data LBA\n");
        }
        if (nsData->mc & BIT1)
        {
            printf("\t\tNamespace supports metadata as part of seperate buffer\n");
        }
    }
    printf("\t\tNumber of LBA Formats Supported: ");
    for (c=0;c<=nsData->nlbaf;c++) 
    {
        printf("LBAF%d ",c);
    }
    printf("\n");
    printf("\t\tCurrent Formatted LBA: LBAF%"PRIu8" ",nsData->flbas & 0x0F);
    if (nsData->flbas & BIT4) 
    {
        printf("[Ext. Meta Data]\n");
    }
    else
    {
        printf("\n");
    }

    for (c=0; c <= nsData->nlbaf; c++) 
    {
        printf("\t\tLBAF%"PRIu32" | Metadata Size %"PRIu16" | LBA Data Size %"PRIu32" ", \
               c,nsData->lbaf[c].ms, (uint32_t) power_Of_Two(nsData->lbaf[c].lbaDS));//removing pow function. Let's not depend on the math lib unless we really need to...-TJE
        switch(nsData->lbaf[c].rp)
        {
        case NVME_NS_LBAF_BEST_RP:
            printf("| BEST PERFORMANCE\n");
            break;
        case NVME_NS_LBAF_BETTER_RP:
            printf("| BETTER PERFORMANCE\n");
            break;
        case NVME_NS_LBAF_GOOD_RP:
            printf("| GOOD PERFORMANCE\n");
            break;
        case NVME_NS_LBAF_DEGRADED_RP:
            printf("| DEGRADED PERFORMANCE\n");
            break;
        default:
            printf("\n");
            break;
        };
    }

    return ret;

}
#endif
#endif