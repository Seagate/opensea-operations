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
// \file smart.c
// \brief This file defines the functions related to SMART features on a drive (attributes, Status check)

#include "operations_Common.h"
#include "smart.h"
#include "usb_hacks.h"
#include "logs.h"
#if !defined (DISABLE_NVME_PASSTHORUGH)
#include "nvme_operations.h"
#endif

int get_SMART_Attributes(tDevice *device, smartLogData * smartAttrs)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE && is_SMART_Enabled(device))
    {
        ataSMARTAttribute currentAttribute;
        uint16_t            smartIter = 0;
        uint8_t *ATAdataBuffer = C_CAST(uint8_t *, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (ATAdataBuffer == NULL)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }
        ret = ata_SMART_Read_Data(device, ATAdataBuffer, LEGACY_DRIVE_SEC_SIZE);
        if (ret == SUCCESS)
        {
            for (smartIter = ATA_SMART_BEGIN_ATTRIBUTES; smartIter < ATA_SMART_END_ATTRIBUTES; smartIter += ATA_SMART_ATTRIBUTE_SIZE)
            {
                currentAttribute.attributeNumber = ATAdataBuffer[smartIter + 0];
                currentAttribute.status = M_BytesTo2ByteValue(ATAdataBuffer[smartIter + 2], ATAdataBuffer[smartIter + 1]);
                currentAttribute.nominal = ATAdataBuffer[smartIter + 3];
                currentAttribute.worstEver = ATAdataBuffer[smartIter + 4];
                currentAttribute.rawData[0] = ATAdataBuffer[smartIter + 5];
                currentAttribute.rawData[1] = ATAdataBuffer[smartIter + 6];
                currentAttribute.rawData[2] = ATAdataBuffer[smartIter + 7];
                currentAttribute.rawData[3] = ATAdataBuffer[smartIter + 8];
                currentAttribute.rawData[4] = ATAdataBuffer[smartIter + 9];
                currentAttribute.rawData[5] = ATAdataBuffer[smartIter + 10];
                currentAttribute.rawData[6] = ATAdataBuffer[smartIter + 11];
                if (currentAttribute.attributeNumber > 0 && currentAttribute.attributeNumber < 255)
                {
                    smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute.attributeNumber].valid = true;
                    memcpy(&smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute.attributeNumber].data, &currentAttribute, sizeof(ataSMARTAttribute));
                    //check if it's warrantied (This should work on Seagate drives at least)
                    if (currentAttribute.status & BIT0)
                    {
                        smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute.attributeNumber].isWarrantied = true;
                    }
                }
            }
            memset(ATAdataBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ata_SMART_Read_Thresholds(device, ATAdataBuffer, LEGACY_DRIVE_SEC_SIZE))
            {
                ataSMARTThreshold currentThreshold;
                for (smartIter = ATA_SMART_BEGIN_ATTRIBUTES; smartIter < ATA_SMART_END_ATTRIBUTES; smartIter += ATA_SMART_ATTRIBUTE_SIZE)
                {
                    currentThreshold.attributeNumber = ATAdataBuffer[smartIter + 0];
                    currentThreshold.thresholdValue = ATAdataBuffer[smartIter + 1];
                    currentThreshold.reservedBytes[0] = ATAdataBuffer[smartIter + 2];
                    currentThreshold.reservedBytes[1] = ATAdataBuffer[smartIter + 3];
                    currentThreshold.reservedBytes[2] = ATAdataBuffer[smartIter + 4];
                    currentThreshold.reservedBytes[3] = ATAdataBuffer[smartIter + 5];
                    currentThreshold.reservedBytes[4] = ATAdataBuffer[smartIter + 6];
                    currentThreshold.reservedBytes[5] = ATAdataBuffer[smartIter + 7];
                    currentThreshold.reservedBytes[6] = ATAdataBuffer[smartIter + 8];
                    currentThreshold.reservedBytes[7] = ATAdataBuffer[smartIter + 9];
                    currentThreshold.reservedBytes[8] = ATAdataBuffer[smartIter + 10];
                    currentThreshold.reservedBytes[9] = ATAdataBuffer[smartIter + 11];
                    if (currentThreshold.attributeNumber > 0 && currentThreshold.attributeNumber < 255)
                    {
                        smartAttrs->attributes.ataSMARTAttr.attributes[currentThreshold.attributeNumber].thresholdDataValid = true;
                        memcpy(&smartAttrs->attributes.ataSMARTAttr.attributes[currentThreshold.attributeNumber].thresholdData, &currentThreshold, sizeof(ataSMARTThreshold));
                    }
                }
            }
        }
        safe_Free_aligned(ATAdataBuffer)
    }
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE) 
    {
        ret = nvme_Get_SMART_Log_Page(device,NVME_ALL_NAMESPACES,(uint8_t *)&smartAttrs->attributes,NVME_SMART_HEALTH_LOG_LEN) ;
    }
    #endif
    else
    {
        ret = NOT_SUPPORTED;
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Getting SMART attributes is not supported on this drive type at this time\n");
        }
    }
    return ret;
}

void get_Attribute_Name(tDevice *device, uint8_t attributeNumber, char **attributeName)
{
    eSeagateFamily isSeagateDrive = is_Seagate_Family(device);
    /*
    I broke the attribute name finder apart because sometimes there's overlap and sometimes there isn't.
    Also, this will let me name the attributes according to the respective specs for each drive.
    */
    memset(*attributeName, 0, MAX_ATTRIBUTE_NAME_LENGTH);
    switch (isSeagateDrive)
    {
    case SEAGATE:
        switch (attributeNumber)
        {
        case 1://read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Rate");
            break;
        case 3://spin up time
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Up Time");
            break;
        case 4://start stop count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Start/Stop Count");
            break;
        case 5://retired sectors count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Retired Sectors Count");
            break;
        case 7://Seek Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Error Rate");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 10: //Spin Retry Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Retry Count");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 18: //Read Error Rate self test
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Head Health Self Assessment");
            break;
        case 174: //Unexpected Power Loss Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unexpected Power Loss Count");
            break;
        case 183://PHY Counter Events
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "PHY Counter Events");
            break;
        case 184://IOEDC Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "IOEDC Count");
            break;
        case 187: //Reported Un-correctable
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Command Timeout");
            break;
        case 189: //High Fly Writes
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "High Fly Writes");
            break;
        case 190: //Airflow Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Airflow Temperature");
            break;
        case 191: //Shock Sensor Counter
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Shock Sensor Counter");
            break;
        case 192: //Emergency Retract Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Emergency Retract Count");
            break;
        case 193: //Load-Unload Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load-Unload Count");
            break;
        case 194: //Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 195: //ECC On the Fly Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "ECC On The Fly Count");
            break;
        case 197: //Pending-Sparing Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Pending-Sparing Count");
            break;
        case 199: //Ultra DMA CRC Error
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Ultra DMA CRC Error");
            break;
        case 200: //Pressure Measurement Limit
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Pressure Measurement Limit");
            break;
        case 230: //Life Curve Status
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Life Curve Status");
            break;
        case 231: //SSD Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left");
            break;
        case 235: //SSD Power Loss Mgmt Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Power Less Mgmt Life Left");
            break;
        case 240: //Head flight Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Head Flight Hours");
            break;
        case 241: //Lifetime Writes from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes From Host");
            break;
        case 242: //Lifetime Reads from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Reads From Host");
            break;
        case 254: //Free Fall Event
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Free Fall Event");
            break;
        default:
            break;
        }
        break;
    case SEAGATE_VENDOR_D://with Seagate for now. Might move sometime
    case SEAGATE_VENDOR_E://with Seagate for now. Might move sometime
        switch (attributeNumber)
        {
        case 1://read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Rate");
            break;
        case 5://retired sectors count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Retired Sectors Count");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 171: //Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Program Fail Count");
            break;
        case 172: //Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Fail Count");
            break;
        case 181: //Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Program Fail Count");
            break;
        case 182: //Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Fail Count");
            break;
        case 194: //Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 201: //Soft Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft Error Rate");
            break;
        case 204: //Soft ECC Correction Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft ECC Correction Rate");
            break;
        case 231: //SSD Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left");
            break;
        case 234: //Lifetime Write to Flash
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes To Flash in GiB");
            break;
        case 241: //Lifetime Writes from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes From Host in GiB");
            break;
        case 242: //Lifetime Reads from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Reads From Host in GiB");
            break;
        case 250: //Lifetime NAND Read Retries
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime NAND Read Retries");
            break;
        default:
            break;
        }
        break;
    case SAMSUNG:
        switch (attributeNumber)
        {
        case 1://read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Rate");
            break;
        case 2: //Throughput Performance
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Throughput Performance");
            break;
        case 3://spin up time
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Up Time");
            break;
        case 4://start stop count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Start/Stop Count");
            break;
        case 5://retired sectors count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Retired Sectors Count");
            break;
        case 7://Seek Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Error Rate");
            break;
        case 8://seek time performance.
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Time Performance");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 10: //Spin Retry Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Retry Count");
            break;
        case 11: //calibration retry count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Calibration Retry Count");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 184://End to End detection
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "End To End Detection");
            break;
        case 187: //Reported Un-correctable
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Command Timeout");
            break;
        case 190: //Airflow Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Airflow Temperature");
            break;
        case 191: //Shock Sensor Counter
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Shock Sensor Counter");
            break;
        case 192: //Emergency Retract Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Emergency Retract Count");
            break;
        case 193: //Load-Unload Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load-Unload Count");
            break;
        case 194: //Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 195: //ECC On the Fly Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "ECC On The Fly Count");
            break;
        case 196: //Re-allocate Sector Event
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Re-allocate Sector Event");
            break;
        case 197: //Pending-Sparing Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Pending Sector Count");
            break;
        case 198://offlince uncorrectable sectors
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Offline Uncorrectable Sectors");
            break;
        case 199: //Ultra DMA CRC Error
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Ultra DMA CRC Error");
            break;
        case 200: //Write Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Write Error Rate");
            break;
        case 201: //Soft Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft Error Rate");
            break;
        case 223: //Load Retry Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load Retry Count");
            break;
        case 225: //Load Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load Cycle Count");
            break;
        default:
            break;
        }
        break;
    case MAXTOR: //names are from here: https://www.smartmontools.org/wiki/AttributesMaxtor
        switch (attributeNumber)
        {
        case 1: //raw read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Raw Read Error Rate");
            break;
        case 2: //throughput performance
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Throughput Performance");
            break;
        case 3: //spin-up time
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Up Time");
            break;
        case 4: //start/stop count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Start/Stop Count");
            break;
        case 5: //Reallocated Sector Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reallocated Sector Count");
            break;
        case 6: //start/stop count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Start/Stop Count");
            break;
        case 7: //seek error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Error Rate");
            break;
        case 8: //seek time performance
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Time Performance");
            break;
        case 9: //power on hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power-On Hours");
            break;
        case 10: //spin-up retry count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin-Up Retry Count");
            break;
        case 11: //calibration retry count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Calibration Retry Count");
            break;
        case 12: //power cycle count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power Cycle Count");
            break;
        case 13: //soft read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft Read Error Rate");
            break;
        case 192: //power-off retract cycle count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power-Off Retract Cycle Count");
            break;
        case 193: //Load/Unload Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load/Unload Cycle Count");
            break;
        case 194: //HDA Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "HDA Temperature");
            break;
        case 195: //Hardware ECC Recovered
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Hardware ECC Recovered");
            break;
        case 196: //Reallocated Event Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Re-allocate Event Count");
            break;
        case 197: //Current Pending Sector Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Current Pending Sector Count");
            break;
        case 198: //Offline Scan Uncorrectable Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Off-line Uncorrectable Count");
            break;
        case 199: //UltraDMA CRC Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Ultra DMA CRC Error Rate");
            break;
        case 200: //Write Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Write Error Rate");
            break;
        case 201: //Soft Read Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft Read Error Rate");
            break;
        case 202: //Data Addres Mark Errors
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Data Address Mark Errors");
            break;
        case 203: //run out cancel
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Run Out Cancel");
            break;
        case 204: //Soft ECC Correction
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft ECC Correction");
            break;
        case 205: //Thermal Asperity Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Thermal Asperity Rate");
            break;
        case 206: //Flying Height
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Flying Height");
            break;
        case 207: //Spin High Current
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin High Current");
            break;
        case 208: //Spin Buzz
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Buzz");
            break;
        case 209: //Offline Seek Performance
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Offline Seek Performance");
            break;
        case 220: //Disk Shift
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Disk Shift");
            break;
        case 221: //G-Sense Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "G-Sense Error Rate");
            break;
        case 222: //Loaded Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Loaded Hours");
            break;
        case 223: //Load/Unload Retry Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load/Unload Retry Count");
            break;
        case 224: //Load Friction
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load Friction");
            break;
        case 225: //Load/Unload Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load/Unload Cycle Count");
            break;
        case 226: //Load-in Time
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Load-In Time");
            break;
        case 227: //Torque Amplification Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Torque Amplification Count");
            break;
        case 228: //Power-Off Retract Cycle
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power-Off Retract Cycle");
            break;
        case 230: //GMR Head Amplitude
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "GMR Head Amplitude");
            break;
        case 231: //Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 240: //Head Flying Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Head Flying Hours");
            break;
        case 250: //Read Error Retry Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Retry Rate");
            break;
        default:
            break;
        }
        break;
    case SEAGATE_VENDOR_B:
    case SEAGATE_VENDOR_C:
        switch (attributeNumber)
        {
        case 1://read error rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Raw Read Error Rate");
            break;
        case 5://retired block count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Retired Block Count");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 100: //Total Erase Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Total Erase Count");
            break;
        case 168: //Min Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Min Power Cycle Count");
            break;
        case 169: //Max power cycle count (seagate-vendor-b-c)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Max Power Cycle Count");
            break;
        case 171: //Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Program Fail Count");
            break;
        case 172: //Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Fail Count");
            break;
        case 174: //Unexpected Power Loss Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unexpected Power Loss Count");
            break;
        case 175: //Maximum Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Maximum Program Fail Count");
            break;
        case 176: //Maximum Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Maximum Erase Fail Count");
            break;
        case 177: //Wear Leveling Count
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Wear Leveling Count");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Endurance Used");
            }
            break;
        case 178: //Used Reserved Block Count for The Worst Die
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Used Reserve Block Count (Chip)");
            break;
        case 179: //Used Reserved Block Count for SSD
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Used Reserve Block Count (Total)");
            break;
        case 180: //reported IOEDC Error In Interval (Seagate/Samsung), End to End Error Detection Rate
            if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "End To End Error Detection Rate");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unused Reserved Block Count (Total)");
            }
            break;
        case 181: //Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Program Fail Count");
            break;
        case 182: //Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Fail Count");
            break;
        case 183://PHY Counter Events (Seagate), SATA Downshift Count (Seagate-vendor-b-c)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SATA Downshift Count");
            break;
        case 184://IOEDC Count (Seagate), End to End Error Detection Count (Seagate-vendor-b-c)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "End To End Error Detection Count");
            break;
        case 187: //Reported Un-correctable
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Command Timeout");
            break;
        case 190: //Airflow Temperature (Seagate), SATA Error Counters (Seagate-vendor-b-c)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SATA Error Counters");
            break;
        case 194: //Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 195: //ECC On the Fly Count (Seagate)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "ECC On The Fly Count");
            break;
        case 196: //Re-allocate Sector Event
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Re-allocate Sector Event");
            break;
        case 197: //Pending-Sparing Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Current Pending Sector Count");
            break;
        case 198://offlince uncorrectable sectors
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Off-line Uncorrectable Sectors");
            break;
        case 199: //Ultra DMA CRC Error
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Ultra DMA CRC Error");
            break;
        case 201: //Uncorrectable Read Error Rate (Seagate-vendor-b-c)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Uncorrectable Read Error Rate");
            break;
        case 204: //Soft ECC Correction Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Soft ECC Correction Rate");
            break;
        case 212: //Phy Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Phy Error Count");
            break;
        case 231: //SSD Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left");
            break;
        case 234: //
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "NAND GiB Written");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Vendor Specific");
            }
            break;
        case 241: //Lifetime Writes from Host
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes From Host in GiB");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Total LBAs Written");
            }
            break;
        case 242: //Lifetime Reads from Host
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Reads From Host in GiB");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Total LBAs Read");
            }
            break;
        case 245: //SSD Life Left (%)
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left %%");
            break;
        case 250: //Lifetime NAND Read Retries
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime NAND Read Retries");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Retry Rate");
            }
            break;
        default:
            break;
        }
        break;
    case SEAGATE_VENDOR_F:
        switch (attributeNumber)
        {
        case 1://UECC error count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "UECC Error count");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 16: //Spare Blocks Available
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spare Blocks Available");
            break;
        case 17: //Remaining Spare Blocks
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Remaining Spare Blocks");
            break;
        case 168: //Sata Phy Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Sata Phy Error Count");
            break;
        case 170: //Bad Block Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Bad Block Count");
            break;
        case 173: //Erase Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Count");
            break;
        case 174: //Unexpected Power Loss Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unexpected Power Loss Count");
            break;
        case 177: //Wear Range Delta
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Wear Range Delta");
            break;
        case 192: //Unexpected power loss count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unexpected Power loss Count");
            break;
        case 194: //Primary Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Primary Temperature");
            break;
        case 218: //CRC Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "CRC Error Count");
            break;
        case 231: //SSD Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left");
            break;
        case 232: //Read failure block count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Failure Block Count");
            break;
        case 233: //NAND GiB written
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "NAND GiB Written");
            break;
        case 235: //NAND sectors written
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "NAND sectors Written");
            break;
        case 241: //Lifetime Writes from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes From Host");
            break;
        case 242: //Lifetime Reads from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Reads From Host");
            break;
		case 246: //Write Protect Detail 
			snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Write Protect Detail");
			break;
        default:
            break;
        }
        break;
    case SEAGATE_VENDOR_G:
        switch (attributeNumber)
        {
        case 1://Raw Read Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Raw Read Error Rate");
            break;
        case 5://Reallocated Sector Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reallocated Sector Count");
            break;
        case 9: //Power on Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 11: //Power Fail Event Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power Fail Event Count");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 100: //Flash Gigabytes Erased
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Flash Gigabytes Erased");
            break;
        case 101: //Lifetime DevSleep Exit Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime DevSleep Exit Count");
            break;
        case 102: //Lifetime PS4 Entry Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime PS4 Entry Count");
            break;
        case 103: //Lifetime PS3 Exit Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime PS3 Exit Count");
            break;
        case 170: //Grown Bad Block Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Grown Bad Block Count");
            break;
        case 171: //Program Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Program Fail Count");
            break;
        case 172: //Erase Fail Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Erase Fail Count");
            break;
        case 173: //Average Program/Erase Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Average Program/Erase Count");
            break;
        case 174: //Unexpected Power Loss Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Unexpected Power Loss Count");
            break;
        case 177: //Wear Range Delta
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Wear Range Delta");
            break;
        case 183: //SATA/PCIe Interface Downshift Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SATA/PCIe Interface Downshift Count");
            break;
        case 184: //End-To-End CRC Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "End-To-End CRC Error Count");
            break;
        case 187: //Uncorrectable ECC Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Uncorrectable ECC Count");
            break;
        case 194: //Primary Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Primary Temperature");
            break;
        case 195: //RAISE ECC Correctable Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "RAISE ECC Correctable Count");
            break;
        case 198: //Uncorrectable Read Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Uncorrectable Read Error Count");
            break;
        case 199: //SATA R-Error (CRC) Error Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SATA R-Error (CRC) Error Count");
            break;
        case 230: //Drive Life Protection Status
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Life Protection Status");
            break;
        case 231: //SSD Life Left
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "SSD Life Left");
            break;
        case 232: //Available Reserved Space
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Available Reserved Space");
            break;
        case 233: //Lifetime Writes to Flash
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes to Flash");
            break;
        case 241: //Lifetime Writes from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Writes From Host");
            break;
        case 242: //Lifetime Reads from Host
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Lifetime Reads From Host");
            break;
        case 243: //Free Space
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Free Space");
            break;
        default:
            break;
        }
        break;
    default:
        switch (attributeNumber)
        {
        case 1://Read Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Read Error Rate");
            break;
        case 3://Spin Up Time
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Up Time");
            break;
        case 4://Start/Stop Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Start/Stop Count");
            break;
        case 5://Retired Sectors Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Retired Sectors Count");
            break;
        case 7://Seek Error Rate
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Seek Error Rate");
            break;
        case 9: //Power On Hours
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Power On Hours");
            break;
        case 10: //Spin Retry Count 
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Spin Retry Count ");
            break;
        case 12: //Drive Power Cycle Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Drive Power Cycle Count");
            break;
        case 187://Reported Un-correctable
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Reported Un-correctable");
            break;
        case 194://Temperature
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Temperature");
            break;
        case 197://Pending-Sparing Count
            snprintf(*attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "Pending-Sparing Count");
            break;
        default:
            break;
        }
        break;
    }
}

static void print_ATA_SMART_Attribute_Raw(ataSMARTValue *currentAttribute, char *attributeName)
{
    uint8_t rawIter = 0;
    if (currentAttribute->data.attributeNumber != 0)
    {
        if (currentAttribute->isWarrantied)
        {
            printf("*");
        }
        else
        {
            printf(" ");
        }
        if (currentAttribute->thresholdDataValid)
        {
            printf("%3"PRIu8" %-35s  %04"PRIX16"h    %02"PRIX8"h     %02"PRIX8"h     %02"PRIX8"h   ", currentAttribute->data.attributeNumber, attributeName, currentAttribute->data.status, currentAttribute->data.nominal, currentAttribute->data.worstEver, currentAttribute->thresholdData.thresholdValue);
        }
        else
        {
            printf("%3"PRIu8" %-35s  %04"PRIX16"h    %02"PRIX8"h     %02"PRIX8"h     N/A   ", currentAttribute->data.attributeNumber, attributeName, currentAttribute->data.status, currentAttribute->data.nominal, currentAttribute->data.worstEver);
        }
        for (rawIter = 0; rawIter < 7; rawIter++)
        {
            printf("%02"PRIX8"", currentAttribute->data.rawData[6 - rawIter]);
        }
        printf("h\n");
    }
    //clear out the attribute name before looping again so we don't show dulicates
    snprintf(attributeName, MAX_ATTRIBUTE_NAME_LENGTH, "                             ");
    return;
}

static void print_Raw_ATA_Attributes(tDevice *device, smartLogData *smartData)
{
    //making the attribute name seperate so that if we add is_Seagate() logic in we can turn on and off printing the name
    char *attributeName = C_CAST(char *, calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char)));
    if (attributeName == NULL)
    {
        perror("Calloc Failure!\n");
        return;
    }
    printf("   # Attribute Name:                     Status: Nominal: Worst: Thresh: Raw (hex):\n");
    for (uint8_t iter = 0; iter < 255; ++iter)
    {
        if (smartData->attributes.ataSMARTAttr.attributes[iter].valid)
        {
            get_Attribute_Name(device, iter, &attributeName);
            print_ATA_SMART_Attribute_Raw(&smartData->attributes.ataSMARTAttr.attributes[iter], attributeName);
        }
    }
    printf("\n* Indicates warranty attribute type, also called Pre-fail attribute type");
    safe_Free(attributeName)
}

/**
 * hex2int
 * take a hex string and convert it to a 32bit number (max 8 hex digits)
 */
uint32_t hex2int(char *hex) {
    uint32_t val = 0;
    while (*hex) {
        // get current character then increment
        char byte = *hex++;
        // transform hex character to the 4bit equivalent number, using the ascii table indexes
        if (byte >= '0' && byte <= '9') byte = byte - '0';
        else if (byte >= 'a' && byte <= 'f') byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F') byte = byte - 'A' + 10;
        // shift 4 to make space for new digit, and add the 4 bits of the new digit 
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

static void print_ATA_SMART_Attribute_Raw_Int_Value(uint8_t* rawdataParameter, int intialIndex, char finalIndex)
{
    int8_t rawIter;
    char rawdata[20] = { 0 };
    char temp[4];
    uint32_t  rawdataIntValue = 0;
    for (rawIter = finalIndex; rawIter >= intialIndex; rawIter--) 
    {
        snprintf(temp, 4, "%02"PRIX8"", rawdataParameter[rawIter]);
        common_String_Concat(rawdata, 20, temp);
    }
    rawdataIntValue = hex2int(rawdata);
    printf("%" PRIu32, rawdataIntValue);
    //sscanf(rawdata, "%x", &rawdataIntValue);
    //printf("%X", rawdataIntValue);
    return;
}

static void print_Analyzed_ATA_Attributes(tDevice *device, smartLogData *smartData)
{
    //making the attribute name seperate so that if we add is_Seagate() logic in we can turn on and off printing the name
    char *attributeName = C_CAST(char *, calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char)));
    if (attributeName == NULL)
    {
        perror("Calloc Failure!\n");
        return;
    }

    eSeagateFamily isSeagateDrive = is_Seagate_Family(device);

    for (uint8_t iter = 0; iter < 255; ++iter)
    {
        if (smartData->attributes.ataSMARTAttr.attributes[iter].valid)
        {
            get_Attribute_Name(device, iter, &attributeName);

            if (smartData->attributes.ataSMARTAttr.attributes[iter].valid)
            {
                if (strlen(attributeName))
                {
                    printf("%u - %s\n", iter, attributeName);
                }
                else
                {
                    printf("%u - Unknown Attribute\n", iter);
                }
                printf("\tAttribute Type(s):\n");
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT0)
                {
                    printf("\t\tPre-fail\n");
                }
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT1)
                {
                    printf("\t\tOnline Data Collection\n");
                }
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT2)
                {
                    printf("\t\tPerformance\n");
                }
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT3)
                {
                    printf("\t\tError Rate\n");
                }
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT4)
                {
                    printf("\t\tEvent Count\n");
                }
                if (smartData->attributes.ataSMARTAttr.attributes[iter].data.status & BIT5)
                {
                    printf("\t\tSelf-Preserving\n");
                }
                printf("\tNominal Value: %"PRIu8"\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.nominal);
                printf("\tWorst Ever Value: %"PRIu8"\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.worstEver);
                if (smartData->attributes.ataSMARTAttr.attributes[iter].thresholdDataValid)
                {
                    if (smartData->attributes.ataSMARTAttr.attributes[iter].thresholdData.thresholdValue == 0)
                    {
                        printf("\tThreshold set to always passing\n");
                    }
                    else if (smartData->attributes.ataSMARTAttr.attributes[iter].thresholdData.thresholdValue == 0xFF)
                    {
                        printf("\tThreshold set to always failing\n");
                    }
                    else
                    {
                        printf("\tThreshold: %"PRIu8"\n", smartData->attributes.ataSMARTAttr.attributes[iter].thresholdData.thresholdValue);
                    }
                }
                switch(isSeagateDrive)
                {
                case SEAGATE:
                    switch (smartData->attributes.ataSMARTAttr.attributes[iter].data.attributeNumber)
                    {
                    case 1://read error rate
                        printf("\tNumber Of Sector Reads: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        printf("\tNumber Of Read Errors: %" PRIu32 "\n", M_BytesTo4ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 3://spin up time
                        //raw unused
                        break;
                    case 4://start stop count
                        printf("\tSpin Up Count: %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 5://retired sectors count
                        printf("\tCurrent Retired Sector Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 7://seek error rate
                        printf("\tNumber Of Seeks: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        printf("\tNumber Of Seek Errors: %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 9://power on hours
                    {
                        uint32_t millisecondsSinceIncrement = M_BytesTo4ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]);
                        uint64_t powerOnMinutes = M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]) * 60;
                        powerOnMinutes += (millisecondsSinceIncrement / 60000);//convert the milliseconds to minutes, then add that to the amount of time we already know
                        printf("\tPower On Hours = %f\n", powerOnMinutes / 60.0);
                    }
                        break;
                    case 10://spin retry count
                        //raw unused
                        break;
                    case 12: //Drive Power Cycle Count
                        printf("\tPower Cycle Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 18://Head health self-assessment
                        printf("\tFailed Heads:\n");
                        //starting at raw 0, bit 0, 0=passing head, 1=failing head
                        {
                            uint32_t headBitmap = M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]);
                            uint16_t badHeadCounter = 0;
                            for (uint16_t bitIter = 0; bitIter < 32; ++bitIter)
                            {
                                if (headBitmap & M_BitN(bitIter))
                                {
                                    ++badHeadCounter;
                                    printf("\t\tHead %" PRIu16 "\n", bitIter);
                                }
                            }
                            if (badHeadCounter == 0)
                            {
                                printf("\t\tNo Failed Heads\n");
                            }
                        }
                        break;
                    case 184://IOEDC Count
                        printf("\tLifetime IOEDC Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 187: //Reported Un-correctable
                        printf("\tTotal # of Reported Uncorrectable Errors To The Host: %" PRIu16 "", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        if (M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]) == 0xFFFF)
                        {
                            printf(" (Counter is maxed out)");
                        }
                        printf("\n");
                        break;
                    case 188: //Command Timeout
                        printf("\tTotal # of command timeouts: %" PRIu16 "", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        if (M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]) == 0xFFFF)
                        {
                            printf(" (Counter is maxed out)");
                        }
                        printf("\n");
                        printf("\tTotal # of commands with > 5 second completion: %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2]));
                        printf("\tTotal # of commands with > 7.5 second completion: %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 189: //High Fly Writes
                        printf("\tTotal # of High Fly Writes Detected: %" PRIu16 "", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        if (M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]) == 0xFFFF)
                        {
                            printf(" (Counter is maxed out)");
                        }
                        printf("\n");
                        break;
                    case 190: //Airflow Temperature
                        printf("\tCurrent Temperature (C): %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        printf("\tLowest Temperature during this power cycle: %" PRIu8 "\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2]);
                        printf("\tHighest Temperature during this power cycle: %" PRIu8 "\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3]);
                        printf("\tNumber of times attribute below threshold: %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 191: //Shock Sensor Counter
                        printf("\tNumber Of Shock Events: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 192: //Emergency Retract Count
                        printf("\tEmergency Retract Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 193: //Load-Unload Count
                        printf("\tLoad Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 194: //Temperature
                        printf("\tCurrent Temperature (C): %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        printf("\tWorst Highest Temperature (C): %" PRIu8 "\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.worstEver);
                        printf("\tWorst Lowest Temperature (C): %" PRIu16 "\n", M_BytesTo2ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 195: //ECC On the Fly Count
                        printf("\tNumber Of Sector Reads: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        printf("\tNumber Of ECC OTF Errors: %" PRIu32 "\n", M_BytesTo4ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]));
                        break;
                    case 197: //Pending-Sparing Count
                        printf("\tCurrent Pending Spare Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 198://offlince uncorrectable sectors
                        printf("\tCurrent Uncorrectable Sector Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 199: //Ultra DMA CRC Error
                        printf("\tCurrent CRC/R_Errs Error Count: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 200: //Pressure Measurement Limit
                        //raw unused
                        break;
                    case 230: //Life Curve Status
                        break;
                    case 231: //SSD Life Left
                        printf("\tSSD Life Left: %" PRIu8 "\n", smartData->attributes.ataSMARTAttr.attributes[iter].data.nominal);
                        break;
                    case 235: //SSD Power Loss Mgmt Life Left
                        break;
                    case 240: //Head flight Hours
                        {
                            uint32_t millisecondsSinceIncrement = M_BytesTo4ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4]);
                            uint64_t headFlightMinutes = M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]) * 60;
                            headFlightMinutes += (millisecondsSinceIncrement / 60000);//convert the milliseconds to minutes, then add that to the amount of time we already know
                            printf("\tHead Flight Hours = %f\n", headFlightMinutes / 60.0);
                        }
                        break;
                    case 241: //Lifetime Writes from Host
                        printf("\tLifetime LBAs Written: %" PRIu64 "\n", M_BytesTo8ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 242: //Lifetime Reads from Host
                        printf("\tLifetime LBAs Read: %" PRIu64 "\n", M_BytesTo8ByteValue(0, smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[5], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[4], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    case 254: //Free Fall Event
                        printf("\tCurrent Free Fall Event Counter: %" PRIu32 "\n", M_BytesTo4ByteValue(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[3], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[2], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[1], smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[0]));
                        break;
                    default:
                        printf("\tRaw Data: ");
                        for (uint8_t rawIter = 0; rawIter < 7; ++rawIter)
                        {
                            printf("%02" PRIX8 "", smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6 - rawIter]);
                        }
                        printf("h\n");
                        break;
                    }
                    break;
                case SEAGATE_VENDOR_G:
                    switch (smartData->attributes.ataSMARTAttr.attributes[iter].data.attributeNumber) 
                    {
                    case 1:
                        printf("\tCorrectable, Soft LDPC correctable errors since last power cycle:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 9: 
                        printf("\tPower On Hours:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 11:
                        printf("\tSuccessful Power Fail Backup Events:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        printf("\tUnsuccessful Power Fail Backup Events:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 4, 5);
                        printf("\n");
                        break;
                    case 12:
                        printf("\tPower Cycles:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 100:
                        printf("\tGB  Erases of Flash:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 101:
                        printf("\tDev Sleep Exits:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 102:
                        printf("\tPS4 entries:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 4);
                        printf("\n");
                        break;
                    case 103:
                        printf("\tPS3 entries:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 4);
                        printf("\n");
                        break;
                    case 171:
                        printf("\tProgram Fail Count:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 172:
                        printf("\tErase Failure Events:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 173:
                        printf("\tProgram/Erase Cycles on All Good Blocks:n");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 174:
                        printf("\tUnexpected Power Loss Power Cycles:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 177:
                        printf("\t100 * [(MW - LW)/MRW]:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 1);
                        printf("\n");
                        break;
                    case 183:
                        printf("\tInterface Downshift Events this Power Cycle:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 2);
                        printf("\n");
                        printf("\tinterface Downshift Events Lifetime:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 3, 6);
                        printf("\n");
                        break;
                    case 184: 
                        printf("\tDetected End-To-End CRC Errors:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 187:
                        printf("\tUncorrectable Codewords:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 194:
                        printf("\tCurrent Temperature (C):");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 1);
                        printf("\n");
                        printf("\tLifetime Maximum Temperature (C):");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 2, 3);
                        printf("\n");
                        printf("\tLifetime Minimum Temperature (C):");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 4, 5);
                        printf("\n");
                        break;
                    case 195:
                        printf("\tRAISE-1 recoveries:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 1);
                        printf("\n");
                        printf("\tRAISE-2 recoveries:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 2, 3);
                        printf("\n");
                        printf("\tNumber of Times RAISE is Used to Restore Date Being Programmed After a Program Failure:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 4, 5);
                        printf("\n");
                        break;
                    case 198:
                        printf("\tUncorrectable Read Errors:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 199:
                        printf("\tSATA Interface CRC Errors Count:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 231:
                        printf("\t0=>TermA Dominated and 1=> TermB Dominated:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 0);
                        printf("\n");
                        printf("\tTerm A value:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 1, 1);
                        printf("\n");
                        printf("\tTerm B value:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 2, 2);
                        printf("\n");
                        break;
                    case 233:
                        printf("\tGB Written of Flash:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 241:
                        printf("\tGB Written to Drive by Host:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 242:
                        printf("\tGB Read from Drive by Host:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        break;
                    case 243:
                        printf("\tFree Space:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 3);
                        printf("\n");
                        printf("\tFree Space Percentage in Hundreths of a Percent:");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 4, 5);
                        printf("\n");
                        break;
                    default: printf("\tRaw Data: ");
                        print_ATA_SMART_Attribute_Raw_Int_Value(smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData, 0, 6);
                        printf("\n");
                        break;
                    }
                    break;
                default:
                    printf("\tRaw Data: ");
                    for (uint8_t rawIter = 0; rawIter < 7; ++rawIter)
                    {                    
                        printf("%02" PRIX8 "", smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6 - rawIter]);
                    }
                    printf("h\n");
                    break;
                }
                
            }
        }
    }
    safe_Free(attributeName)
    return;
}

int print_SMART_Attributes(tDevice *device, eSMARTAttrOutMode outputMode)
{
    int ret = UNKNOWN;
    smartLogData smartData; 
    memset(&smartData,0,sizeof(smartLogData));
    ret = get_SMART_Attributes(device,&smartData);
    if (ret != SUCCESS) 
    {
        if (ret == NOT_SUPPORTED) 
        {
            printf("Printing SMART attributes is not supported on this drive type at this time\n");
        }
        else
        {
            printf("Error retreiving the logs. \n");
        }
    }
    else
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            if (outputMode == SMART_ATTR_OUTPUT_RAW)
            {
                print_Raw_ATA_Attributes(device, &smartData);
            }
            else if (outputMode == SMART_ATTR_OUTPUT_ANALYZED)
            {
                print_Analyzed_ATA_Attributes(device, &smartData);
            }
            else
            {
                ret = BAD_PARAMETER;
            }
        }
        #if !defined(DISABLE_NVME_PASSTHROUGH)
        else if (device->drive_info.drive_type == NVME_DRIVE) 
        {
                uint32_t temperature = ((smartData.attributes.nvmeSMARTAttr.temperature[1] << 8) |
                    smartData.attributes.nvmeSMARTAttr.temperature[0]) - 273;

                printf("Critical Warnings                   : %#x\n", smartData.attributes.nvmeSMARTAttr.criticalWarning & 0x1F);
                printf("Temperature                         : %"PRIu32" C\n", temperature);
                printf("Available Spare                     : %"PRIu8"%%\n", smartData.attributes.nvmeSMARTAttr.availSpare);
                printf("Available Spare Threshold           : %"PRIu8"%%\n", smartData.attributes.nvmeSMARTAttr.spareThresh);
                printf("Percentage Used                     : %"PRIu8"%%\n", smartData.attributes.nvmeSMARTAttr.percentUsed);
                printf("Data Units Read                     : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.dataUnitsRead));
                printf("Data Units Written                  : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.dataUnitsWritten));
                printf("Host Read Commands                  : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.hostReads));
                printf("Host Write Commands                 : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.hostWrites));
                printf("Controller Busy Time                : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.ctrlBusyTime));                
                printf("Power Cycles                        : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.powerCycles));
                printf("Power On Hours (POH)                : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.powerOnHours));
                printf("Unsafe Shutdowns                    : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.unsafeShutdowns));
                printf("Media Errors                        : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.mediaErrors));
                printf("Num. Of Error Info. Log             : %.0f\n", convert_128bit_to_double(smartData.attributes.nvmeSMARTAttr.numErrLogEntries));
                printf("Warning Composite Temperature Time  : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.warningTempTime);
                printf("Critical Composite Temperature Time : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.criticalCompTime);
                for (uint8_t temperatureSensorCount = 0; temperatureSensorCount < 8; temperatureSensorCount++) {
                    if (smartData.attributes.nvmeSMARTAttr.tempSensor[temperatureSensorCount] != 0) {
                        uint16_t temperatureSensor = smartData.attributes.nvmeSMARTAttr.tempSensor[temperatureSensorCount] - 273;
                        printf("Temperature Sensor %"PRIu8"                : %"PRIu16" C\n", (temperatureSensorCount + 1), temperatureSensor);
                    }
                }
                printf("Thermal Management T1 Trans Count   : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.thermalMgmtTemp1TransCount);
                printf("Thermal Management T2 Trans Count   : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.thermalMgmtTemp2TransCount);
                printf("Thermal Management T1 Total Time    : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.totalTimeThermalMgmtTemp1);
                printf("Thermal Management T2 Total Time    : %"PRIu32"\n", smartData.attributes.nvmeSMARTAttr.totalTimeThermalMgmtTemp2);
        }
        #endif
        else
        {
            //shouldn't get here.
            ret = NOT_SUPPORTED;
        }
    }
    return ret;
}

bool is_SMART_Command_Transport_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word206 & BIT0)
        {
            supported = true;
        }
    }
    return supported;
}

bool is_SMART_Error_Logging_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if ((device->drive_info.IdentifyData.ata.Word084 != 0x0000 && device->drive_info.IdentifyData.ata.Word084 != 0xFFFF && device->drive_info.IdentifyData.ata.Word084 & BIT0)
            ||
            (device->drive_info.IdentifyData.ata.Word087 != 0x0000 && device->drive_info.IdentifyData.ata.Word087 != 0xFFFF && device->drive_info.IdentifyData.ata.Word087 & BIT0)
            )
        {
            supported = true;
        }
    }
    return supported;
}

int get_ATA_SMART_Status_From_SCT_Log(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    if (is_SMART_Command_Transport_Supported(device))
    {
        bool checkData = false;
        //try reading the SCT status log (ACS4 adds SMART status to this log)
        bool readSCTStatusWithSMARTCommand = device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly;//USB hack
        uint8_t sctStatus[512] = { 0 };
        if (device->drive_info.ata_Options.generalPurposeLoggingSupported && !readSCTStatusWithSMARTCommand &&
            SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_SCT_COMMAND_STATUS, 0, sctStatus, 512, 0)
            )
        {
            checkData = true;
        }
        else
        {
            if (is_SMART_Error_Logging_Supported(device))
            {
                if (SUCCESS == ata_SMART_Read_Log(device, ATA_SCT_COMMAND_STATUS, sctStatus, 512))
                {
                    checkData = true;
                }
            }
        }
        if (checkData)
        {
            uint16_t sctFormatVersion = M_BytesTo2ByteValue(sctStatus[1], sctStatus[0]);
            if (sctFormatVersion > 2)
            {
                uint16_t smartStatus = M_BytesTo2ByteValue(sctStatus[215], sctStatus[214]);
                //SMART status
                switch (smartStatus)
                {
                case 0xC24F:
                    ret = SUCCESS;
                    break;
                case 0x2CF4:
                    ret = FAILURE;
                    break;
                default:
                    ret = UNKNOWN;
                    break;
                }
            }
        }
    }
    return ret;
}

int ata_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
{
    int ret = NOT_SUPPORTED; //command return value
    if (is_SMART_Enabled(device))
    {
        bool attemptCheckWithAttributes = false;
        //if (supports_ATA_Return_SMART_Status_Command(device))//USB hack. Will return true on IDE/SCSI interface. May return true or false otherwise depending on what device we detect
        {
            ret = ata_SMART_Return_Status(device);
            if (device->drive_info.lastCommandRTFRs.lbaMid == ATA_SMART_SIG_MID && device->drive_info.lastCommandRTFRs.lbaHi == ATA_SMART_SIG_HI)
            {
                ret = SUCCESS;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaMid == ATA_SMART_BAD_SIG_MID && device->drive_info.lastCommandRTFRs.lbaHi == ATA_SMART_BAD_SIG_HI)
            {
                //SMART is tripped
                ret = FAILURE;
            }
            else
            {
                //try SCT status log first...
                ret = get_ATA_SMART_Status_From_SCT_Log(device);
                if (ret == UNKNOWN && device->drive_info.interface_type != IDE_INTERFACE)
                {
                    //try use SAT translation instead
                    ret = scsi_SMART_Check(device, tripInfo);
                    if (ret == UNKNOWN)
                    {
                        attemptCheckWithAttributes = true;
                    }
                }
                else
                {
                    attemptCheckWithAttributes = true;
                }
            }
        }
//      else
//      {
//          //this device doesn't support getting SMART status from return status command (translator bug)
//          //try other methods.
//          ret = get_ATA_SMART_Status_From_SCT_Log(device);
//          if (ret == UNKNOWN && device->drive_info.interface_type != IDE_INTERFACE)
//          {
//              //try use SAT translation instead
//              ret = scsi_SMART_Check(device, tripInfo);
//              if (ret == UNKNOWN)
//              {
//                  attemptCheckWithAttributes = true;
//              }
//          }
//          else
//          {
//              attemptCheckWithAttributes = true;
//          }
//      }
        
        if ((ret == FAILURE && tripInfo) || ret == UNKNOWN || ret == NOT_SUPPORTED || attemptCheckWithAttributes)
        {
            smartLogData attributes;
            memset(&attributes, 0, sizeof(smartLogData));
            if (SUCCESS == get_SMART_Attributes(device, &attributes))
            {
                //go through and compare attirbutes to thresholds (as long as the thresholds were able to be read!!!)
                for (uint16_t counter = 0; counter < 256; ++counter)
                {
                    if (attributes.attributes.ataSMARTAttr.attributes[counter].valid)
                    {
                        if (attributes.attributes.ataSMARTAttr.attributes[counter].thresholdDataValid)
                        {
                            if (attributes.attributes.ataSMARTAttr.attributes[counter].thresholdData.thresholdValue == 0)
                            {
                                //skip, this is an always passing attribute
                            }
                            else if (attributes.attributes.ataSMARTAttr.attributes[counter].thresholdData.thresholdValue == 0xFF)
                            {
                                //This is an always failing attribute! (make note on the screen)
                                ret = FAILURE;//this should override the "unknown" return value if it was set
                                if (tripInfo)
                                {
                                    tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_ATA;
                                    tripInfo->ataAttribute.attributeNumber = attributes.attributes.ataSMARTAttr.attributes[counter].data.attributeNumber;
                                    tripInfo->ataAttribute.nominalValue = attributes.attributes.ataSMARTAttr.attributes[counter].data.nominal;
                                    tripInfo->ataAttribute.thresholdValue = attributes.attributes.ataSMARTAttr.attributes[counter].thresholdData.thresholdValue;
                                    char *attributeName = C_CAST(char *, calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char)));
                                    get_Attribute_Name(device, tripInfo->ataAttribute.attributeNumber, &attributeName);
                                    if (strlen(attributeName))
                                    {
                                        //use the name in the error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "%s [%" PRIu8 "] set to test trip!", attributeName, tripInfo->ataAttribute.attributeNumber);
                                        tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
                                    }
                                    else
                                    {
                                        //Couldn't look up the name, so set a generic error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "Attribute %" PRIu8 " set to test trip!", tripInfo->ataAttribute.attributeNumber);
                                        tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
                                    }
                                }
                                break;
                            }
                            else if (attributes.attributes.ataSMARTAttr.attributes[counter].data.nominal <= attributes.attributes.ataSMARTAttr.attributes[counter].thresholdData.thresholdValue)
                            {
                                //found the attribute causing the problem!!!
                                ret = FAILURE;//this should override the "unknown" return value if it was set
                                if (tripInfo)
                                {
                                    tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_ATA;
                                    tripInfo->ataAttribute.attributeNumber = attributes.attributes.ataSMARTAttr.attributes[counter].data.attributeNumber;
                                    tripInfo->ataAttribute.nominalValue = attributes.attributes.ataSMARTAttr.attributes[counter].data.nominal;
                                    tripInfo->ataAttribute.thresholdValue = attributes.attributes.ataSMARTAttr.attributes[counter].thresholdData.thresholdValue;
                                    char *attributeName = C_CAST(char *, calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char)));
                                    get_Attribute_Name(device, tripInfo->ataAttribute.attributeNumber, &attributeName);
                                    if (strlen(attributeName))
                                    {
                                        //use the name in the error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "%s [%" PRIu8 "] tripped! Nominal Value %" PRIu8 " below Threshold %" PRIu8 "", attributeName, tripInfo->ataAttribute.attributeNumber, tripInfo->ataAttribute.nominalValue, tripInfo->ataAttribute.thresholdValue);
                                        tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
                                    }
                                    else
                                    {
                                        //Couldn't look up the name, so set a generic error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "Attribute %" PRIu8 " tripped! Nominal Value %" PRIu8 " below Threshold %" PRIu8 "", tripInfo->ataAttribute.attributeNumber, tripInfo->ataAttribute.nominalValue, tripInfo->ataAttribute.thresholdValue);
                                        tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return ret;
}

static void translate_SCSI_SMART_Sense_To_String(uint8_t asc, uint8_t ascq, char *reasonString, uint8_t reasonStringMaxLength, uint8_t *reasonStringOutputLength)
{
    switch (asc)
    {
    case 0x5D:
        if (/* ascq >= 0 && */ ascq < 0x10)
        {
            switch (ascq)
            {
            case 0x00:
                snprintf(reasonString, reasonStringMaxLength, "Failure Prediction Threshold Exceeded");
                break;
            case 0x01:
                snprintf(reasonString, reasonStringMaxLength, "Media Failure Prediction Threshold Exceeded");
                break;
            case 0x02:
                snprintf(reasonString, reasonStringMaxLength, "Logical Unit Failure Prediction Threshold Exceeded");
                break;
            case 0x03:
                snprintf(reasonString, reasonStringMaxLength, "Spare Area Exhaustion Prediction Threshold Exceeded");
                break;
            default:
                break;
            }
        }
        else if (ascq < 0x70)
        {
            bool impendingFailureMissing = false;
            bool failureReasonMissing = false;
            char impendingFailure[40] = { 0 };
            switch (ascq >> 4)
            {
            case 1:
                snprintf(impendingFailure, 40, "Hardware Impending Failure");
                break;
            case 2:
                snprintf(impendingFailure, 40, "Controller Impending Failure");
                break;
            case 3:
                snprintf(impendingFailure, 40, "Data Channel Impending Failure");
                break;
            case 4:
                snprintf(impendingFailure, 40, "Servo Impending Failure");
                break;
            case 5:
                snprintf(impendingFailure, 40, "Spindle Impending Failure");
                break;
            case 6:
                snprintf(impendingFailure, 40, "Firmware Impending Failure");
                break;
            default:
                impendingFailureMissing = true;
                break;
            }
            char failureReason[40] = { 0 };
            switch (ascq & 0x0F)
            {
            case 0x00:
                snprintf(impendingFailure, 40, "General Hard Drive Failure");
                break;
            case 0x01:
                snprintf(impendingFailure, 40, "Drive Error Rate Too High");
                break;
            case 0x02:
                snprintf(impendingFailure, 40, "Data Error Rate Too High");
                break;
            case 0x03:
                snprintf(impendingFailure, 40, "Seek Error Rate Too High");
                break;
            case 0x04:
                snprintf(impendingFailure, 40, "Too Many Block Reassigns");
                break;
            case 0x05:
                snprintf(impendingFailure, 40, "Access Times Too High");
                break;
            case 0x06:
                snprintf(impendingFailure, 40, "Start Unit Times Too high");
                break;
            case 0x07:
                snprintf(impendingFailure, 40, "Channel Parametrics");
                break;
            case 0x08:
                snprintf(impendingFailure, 40, "Controller Detected");
                break;
            case 0x09:
                snprintf(impendingFailure, 40, "Throughput Performance");
                break;
            case 0x0A:
                snprintf(impendingFailure, 40, "Seek Time Performance");
                break;
            case 0x0B:
                snprintf(impendingFailure, 40, "Spin-up Retry Count");
                break;
            case 0x0C:
                snprintf(impendingFailure, 40, "Drive Calibration Retry Count");
                break;
            case 0x0D:
                snprintf(impendingFailure, 40, "Power Loss Protection Circuit");
                break;
            default:
                failureReasonMissing = true;
                break;
            }
            if (failureReasonMissing || impendingFailureMissing)
            {
                if (impendingFailureMissing)
                {
                    snprintf(reasonString, reasonStringMaxLength, "unknown ascq %" PRIu8 "", ascq);
                }
                else
                {
                    snprintf(reasonString, reasonStringMaxLength, "%s - unknown ascq %" PRIu8 "", impendingFailure, ascq);
                }
            }
            else
            {
                snprintf(reasonString, reasonStringMaxLength, "%s - %s", impendingFailure, failureReason);
            }
        }
        else
        {
            switch (ascq)
            {
            case 0x73:
                snprintf(reasonString, reasonStringMaxLength, "Media Impending Failure Endurance Limit Met");
                break;
            case 0xFF:
                snprintf(reasonString, reasonStringMaxLength, "Failure Prediction Threshold Exceeded (False)");
                break;
            default:
                break;
            }
        }
        break;
    case 0x0B:
        switch (ascq)
        {
        case 0x00:
            //This only means "WARNING" which isn't very useful....so I'm not translating it right now. - TJE
            break;
        case 0x01:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Specified Temperature Exceeded");
            break;
        case 0x02:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Enclosure Degraded");
            break;
        case 0x03:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Background Self-Test Failed");
            break;
        case 0x04:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Background Pre-scan Detected Medium Error");
            break;
        case 0x05:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Background Medium Scan Detected Medium Error");
            break;
        case 0x06:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Non-Volatile Cache Now Volatile");
            break;
        case 0x07:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Degraded Power To Non-Volatile Cache");
            break;
        case 0x08:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Power Loss Expected");
            break;
        case 0x09:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Device Statistics Notification Active");
            break;
        case 0x0A:
            snprintf(reasonString, reasonStringMaxLength, "Warning - High Critical Temperature Limit Exceeded");
            break;
        case 0x0B:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Low Critical Tempterure Limit Exceeded");
            break;
        case 0x0C:
            snprintf(reasonString, reasonStringMaxLength, "Warning - High Operating Temperature Limit Exceeded");
            break;
        case 0x0D:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Low Operating Temperature Limit Exceeded");
            break;
        case 0x0E:
            snprintf(reasonString, reasonStringMaxLength, "Warning - High Critical Humidity Limit Exceeded");
            break;
        case 0x0F:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Low Critical Humidity Limit Exceeded");
            break;
        case 0x10:
            snprintf(reasonString, reasonStringMaxLength, "Warning - High Operating Humidity Limit Exceeded");
            break;
        case 0x11:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Low Operating Humidity Limit Exceeded");
            break;
        case 0x12:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Microcode Security At Risk");
            break;
        case 0x13:
            snprintf(reasonString, reasonStringMaxLength, "Warning - Microcode Digital Signature Validation Failure");
            break;
        default:
            break;
        }
        break;
    default:
        //Don't do anything. This is not a valid sense combination for a SMART trip
        break;
    }
    *reasonStringOutputLength = C_CAST(uint8_t, strlen(reasonString));
}
//
int scsi_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
{
    int ret = NOT_SUPPORTED;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Starting SCSI SMART Check\n");
    }

    informationalExceptionsLog infoExceptionsLog;
    informationalExceptionsControl infoExceptionsControl;
    memset(&infoExceptionsLog, 0, sizeof(informationalExceptionsLog));
    memset(&infoExceptionsControl, 0, sizeof(informationalExceptionsControl));
    bool sendRequestSense = false;
    bool readModePage = false;
    bool temporarilyEnableMRIEMode6 = false;//This will hold if we are changing the mode from a value of 1-5 to 6. DO NOT CHANGE IT IF IT IS ZERO! We should return NOT_SUPPORTED in this case. - TJE
    uint32_t delayTimeMilliseconds = 0;//This will be used to make a delay only if the interval is a value less than 1000milliseconds, otherwise we'll change the mode page.
    //get informational exceptions data from the drive first
    if(SUCCESS == get_SCSI_Informational_Exceptions_Info(device, MPC_CURRENT_VALUES, &infoExceptionsControl, &infoExceptionsLog) || infoExceptionsLog.isValid)
    {
        if (infoExceptionsLog.isValid)
        {
            //This is supposed to be the most consistent way of determining this...it should work always so long as the page is supported.
            if (infoExceptionsLog.additionalSenseCode == 0x5D)
            {
                ret = FAILURE;
                if (tripInfo)
                {
                    tripInfo->informationIsValid = true;
                    tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_SCSI;
                    tripInfo->scsiSenseCode.asc = infoExceptionsLog.additionalSenseCode;
                    tripInfo->scsiSenseCode.ascq = infoExceptionsLog.additionalSenseCodeQualifier;
                    translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX, &tripInfo->reasonStringLength);
                }
            }
            else if (infoExceptionsLog.additionalSenseCode == 0x0B)
            {
                ret = IN_PROGRESS;//using this to signify that a warning is being generated from the drive.
                if (tripInfo)
                {
                    tripInfo->informationIsValid = true;
                    tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_SCSI;
                    tripInfo->scsiSenseCode.asc = infoExceptionsLog.additionalSenseCode;
                    tripInfo->scsiSenseCode.ascq = infoExceptionsLog.additionalSenseCodeQualifier;
                    translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX, &tripInfo->reasonStringLength);
                }
            }
            else
            {
                ret = SUCCESS;
            }
        }
        else
        {
            //got the log and mode page...need to check mode page settings to see if an error get's logged and the MRIE value so we can attempt a request sense.
            if (infoExceptionsControl.isValid)
            {
                readModePage = true;
                switch (infoExceptionsControl.mrie)
                {
                case 1://asynchronous event reporting (not supported on Seagate drives)
                case 2://Generate Unit attention (sense key 6, asc = 5D. Fail command, no data transfer)
                case 3://Conditionally generate recovered error (sense key 1, asc 5D on command that normall generates good status. Honors PER bit)
                case 4://Unconditionally generate recovered error (sense key 1, asc 5D on command that normall generates good status. Independent of PER bit)
                case 5://Generate No Sense (sense key 0, asc 5D)
                    temporarilyEnableMRIEMode6 = true;
                    sendRequestSense = true;
                    break;
                case 6://issue request sense. We may need to change the interval or reporting count first....
                    sendRequestSense = true;
                    //we need to check the interval and the report count fields...depending on what these are, we may need to either wait or make a mode page change
                    if (infoExceptionsControl.intervalTimer == 0 || infoExceptionsControl.intervalTimer == UINT32_MAX || infoExceptionsControl.intervalTimer > 10)
                    {
                        temporarilyEnableMRIEMode6 = true;
                    }
                    else
                    {
                        delayTimeMilliseconds = 100 * infoExceptionsControl.intervalTimer;
                    }
                    if (infoExceptionsControl.reportCount != 0)//we want an infinite number of times just so that we always generate it with our request sense command
                    {
                        temporarilyEnableMRIEMode6 = true;
                    }
                    break;
                case 0://not enabled
                default://unknown or not supported value
                    //not enabled, return NOT_SUPPORTED. Make them use the --setMRIE option to change to something else first
                    ret = NOT_SUPPORTED;
                    break;
                }
            }
            else
            {
                //uhh...just try request sense or return NOT_SUPPORTED???...I don't think this case should ever get hit - TJE
                sendRequestSense = true;
            }
        }
    }
    else
    {
        //This device doesn't support the log page or mode page...so just try a request sense and see what the sense data gives.
        sendRequestSense = true;
    }
    if (temporarilyEnableMRIEMode6)
    {
        delayTimeMilliseconds = 100;//100 milliseconds to match our temporary change
        //change MRIE mode to 6, PS = 0 and SP = false to change this temporarily so we can issue a request sense.
        informationalExceptionsControl tempControl;
        //copy current settings over
        memcpy(&tempControl, &infoExceptionsControl, sizeof(informationalExceptionsControl));
        tempControl.mrie = 6;//generate error upon request
        tempControl.reportCount = 0;//always generate errors
        tempControl.intervalTimer = 1;//100 milliseconds
        tempControl.ewasc = true;//turn on warnings for the check since we are making a temporary change...TODO: determine above if we should turn this on all the time or not (if it's not already on)
        tempControl.ps = false;//make sure we don't save this value!
        set_SCSI_Informational_Exceptions_Info(device, false, &tempControl);//save bit to false...don't want to save this change
    }
    if (delayTimeMilliseconds > 0 && delayTimeMilliseconds <= 1000)//do not wait longer than a second...should be caught above, but just in case....-TJE
    {
        delay_Milliseconds(delayTimeMilliseconds);
    }
    if (sendRequestSense)
    {
        uint8_t *senseData = C_CAST(uint8_t*, calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        scsi_Request_Sense_Cmd(device, false, senseData, SPC3_SENSE_LEN);
        uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
        get_Sense_Key_ASC_ASCQ_FRU(senseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
        if (asc == 0x5D)
        {
            ret = FAILURE;
            if (tripInfo)
            {
                tripInfo->informationIsValid = true;
                tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_SCSI;
                tripInfo->scsiSenseCode.asc = asc;
                tripInfo->scsiSenseCode.ascq = ascq;
                translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX, &tripInfo->reasonStringLength);
            }
        }
        else if (asc == 0x0B)
        {
            ret = IN_PROGRESS;//using this to signify that a warning is being generated from the drive.
            if (tripInfo)
            {
                tripInfo->informationIsValid = true;
                tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_SCSI;
                tripInfo->scsiSenseCode.asc = asc;
                tripInfo->scsiSenseCode.ascq = ascq;
                translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX, &tripInfo->reasonStringLength);
            }
        }
        else
        {
            if (readModePage)
            {
                ret = SUCCESS;
            }
            else
            {
                ret = UNKNOWN;
            }
        }
        safe_Free_aligned(senseData)
    }
    if (temporarilyEnableMRIEMode6)
    {
        //Change back to the user's saved settings
        informationalExceptionsControl savedControlSettings;
        memset(&savedControlSettings, 0, sizeof(informationalExceptionsControl));
        if(SUCCESS == get_SCSI_Informational_Exceptions_Info(device, MPC_SAVED_VALUES, &savedControlSettings, NULL))
        {
            if (SUCCESS != set_SCSI_Informational_Exceptions_Info(device, true, &savedControlSettings))
            {
                //try again with the save bit set to false...shouldn't happen but we need to try to get this back to the user's other settings.
                set_SCSI_Informational_Exceptions_Info(device, false, &savedControlSettings);
            }
        }
        //no else...we tried our best...-TJE
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
int nvme_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
{
    int ret = UNKNOWN;
    uint8_t smartLogPage[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    nvmeGetLogPageCmdOpts smartPageOpts;
    memset(&smartPageOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    smartPageOpts.addr = smartLogPage;
    smartPageOpts.dataLen = LEGACY_DRIVE_SEC_SIZE;
    smartPageOpts.lid = NVME_LOG_SMART_ID;
    smartPageOpts.nsid = UINT32_MAX;//requesting controller page, not namespace page. - TJE
    if (SUCCESS == nvme_Get_Log_Page(device, &smartPageOpts))
    {
        //check the critical warning byte! (Byte 0)
        if (smartLogPage[0] > 0)
        {
            //TODO: Return the reason for the failure! - TJE
            ret = FAILURE;
        }
        else
        {
            ret = SUCCESS;
        }
        if (tripInfo && ret == FAILURE)
        {
            tripInfo->additionalInformationType = SMART_TRIP_INFO_TYPE_NVME;
            tripInfo->informationIsValid = true;
            if (smartLogPage[0] & BIT0)
            {
                tripInfo->nvmeCriticalWarning.spareSpaceBelowThreshold = true;
                snprintf(tripInfo->reasonString, UINT8_MAX, "Available Spare Space has fallen below the threshold");
                tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
            }
            if (smartLogPage[0] & BIT1)
            {
                tripInfo->nvmeCriticalWarning.temperatureExceedsThreshold = true;
                snprintf(tripInfo->reasonString, UINT8_MAX,  "Temperature is above an over termperature threshold or below an under temperature threshold");
                tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
            }
            if (smartLogPage[0] & BIT2)
            {
                tripInfo->nvmeCriticalWarning.nvmSubsystemDegraded = true;
                snprintf(tripInfo->reasonString, UINT8_MAX,  "NVM subsystem reliability has been degraded due to significant media related errors or an internal error that degrades reliability");
                tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
            }
            if (smartLogPage[0] & BIT3)
            {
                tripInfo->nvmeCriticalWarning.mediaReadOnly = true;
                snprintf(tripInfo->reasonString, UINT8_MAX,  "Media has been placed in read only mode");
                tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
            }
            if (smartLogPage[0] & BIT4)
            {
                tripInfo->nvmeCriticalWarning.volatileMemoryBackupFailed = true;
                snprintf(tripInfo->reasonString, UINT8_MAX,  "Volatile Memory backup device has failed");
                tripInfo->reasonStringLength = C_CAST(uint8_t, strlen(tripInfo->reasonString));
            }
            if (smartLogPage[0] & BIT5)
            {
                tripInfo->nvmeCriticalWarning.reservedBit5 = true;
            }
            if (smartLogPage[0] & BIT6)
            {
                tripInfo->nvmeCriticalWarning.reservedBit6 = true;
            }
            if (smartLogPage[0] & BIT7)
            {
                tripInfo->nvmeCriticalWarning.reservedBit7 = true;
            }
        }
    }

    return ret;
}
#endif

int run_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
{
    int result = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        result = scsi_SMART_Check(device, tripInfo);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        result = ata_SMART_Check(device, tripInfo);
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
#if !defined (DISABLE_NVME_PASSTHROUGH)
        result = nvme_SMART_Check(device, tripInfo);
#else
        //No SCSI translation exists for this, so return not_supported
        result = NOT_SUPPORTED;
#endif
    }
    return result;
}


bool is_SMART_Enabled(tDevice *device)
{
    bool enabled = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        //check identify data
        if (device->drive_info.IdentifyData.ata.Word085 != 0x0000 && device->drive_info.IdentifyData.ata.Word085 != 0xFFFF && device->drive_info.IdentifyData.ata.Word085 & BIT0)
        {
            enabled = true;
        }
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        //SMART/health is built in and not enable-able or disable-able - TJE
        enabled = true;
        break;
#endif
    case SCSI_DRIVE:
    {
        //read the informational exceptions mode page and check MRIE value for something other than 0
        uint8_t *infoExceptionsControl = C_CAST(uint8_t*, calloc_aligned(12 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!infoExceptionsControl)
        {
            perror("calloc failure for infoExceptionsControl");
            return false;
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_INFORMATION_EXCEPTIONS_CONTROL, 12 + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, infoExceptionsControl))
        {
            if (M_Nibble0(infoExceptionsControl[MODE_PARAMETER_HEADER_10_LEN + 3]) > 0)
            {
                enabled = true;
            }
        }
        else if (SUCCESS == scsi_Mode_Sense_6(device, MP_INFORMATION_EXCEPTIONS_CONTROL, 12 + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_CURRENT_VALUES, infoExceptionsControl))
        {
            if (M_Nibble0(infoExceptionsControl[MODE_PARAMETER_HEADER_6_LEN + 3]) > 0)
            {
                enabled = true;
            }
        }
        safe_Free_aligned(infoExceptionsControl)
    }
    break;
    default:
        break;
    }
    return enabled;
}

bool is_SMART_Check_Supported(tDevice *device)
{
    bool supported = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        //On ATA, if SMART is enabled, then we can do a SMART check
        supported = is_SMART_Enabled(device);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        supported = true;
        break;
#endif
    case SCSI_DRIVE:
        //For SMART Check on SCSI, first look for the informational exceptions log page to be supported...then look for the mode page. At least one of these has to be available to do this.
    {
        uint32_t logSize = 0;
        if (SUCCESS == get_SCSI_Log_Size(device, LP_INFORMATION_EXCEPTIONS, 0, &logSize) && logSize > 0)
        {
            supported = true;
        }
        else
        {
            //check if the mode page is supported...at least then we can attempt the other methods we have in this code to check for a trip.
            uint8_t informationalExceptionsModePage[MP_INFORMATION_EXCEPTIONS_LEN + MODE_PARAMETER_HEADER_10_LEN] = { 0 };
            if (SUCCESS == scsi_Mode_Sense_10(device, MP_INFORMATION_EXCEPTIONS_CONTROL, MP_INFORMATION_EXCEPTIONS_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, informationalExceptionsModePage))
            {
                //check the page code to be sure we got the right page.
                if (M_GETBITRANGE(informationalExceptionsModePage[0], 5, 0) == 0x1C && informationalExceptionsModePage[1] >= 0x0A)
                {
                    supported = true;
                }
            }
        }
    }
        break;
    default:
        break;
    }
    return supported;
}

int get_Pending_List_Count(tDevice *device, uint32_t *pendingCount)
{
    int ret = SUCCESS;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //get from SMART attribute 197 or from device statistics log
        bool pendingCountFound = false;
        if (device->drive_info.softSATFlags.deviceStatisticsSupported)
        {
            //printf("In Device Statistics\n");
            uint8_t rotatingMediaStatistics[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, rotatingMediaStatistics, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                uint64_t *qWordPtr = (uint64_t*)&rotatingMediaStatistics[0];
                if (qWordPtr[7] & BIT63 && qWordPtr[7] & BIT62)
                {
                    *pendingCount = M_DoubleWord0(qWordPtr[7]);
                    pendingCountFound = true;
                }
            }
        }
        if (!pendingCountFound && is_SMART_Enabled(device))
        {
            //printf("In Attributes\n");
            //try SMART data
            smartLogData smartData;
            memset(&smartData, 0, sizeof(smartLogData));
            if (SUCCESS == get_SMART_Attributes(device, &smartData))
            {
                //now get the count from the SMART attribute raw data
                if (smartData.attributes.ataSMARTAttr.attributes[197].valid)
                {
                    *pendingCount = M_BytesTo4ByteValue(smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[3], \
                        smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[2], \
                        smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[1], \
                        smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[0]);
                    pendingCountFound = true;
                }
            }
        }
        if (!pendingCountFound)
        {
            ret = NOT_SUPPORTED;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //get by reading the pending defects log page (SBC4) parameter 0, which is a count
        uint8_t pendingLog[12] = { 0 };
        if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_PENDING_DEFECTS, 1, 0, pendingLog, 12))
        {
            //parameter 0 has the count
            *pendingCount = M_BytesTo4ByteValue(pendingLog[LOG_PAGE_HEADER_LENGTH + 4], pendingLog[LOG_PAGE_HEADER_LENGTH + 5], pendingLog[LOG_PAGE_HEADER_LENGTH + 6], pendingLog[LOG_PAGE_HEADER_LENGTH + 7]);
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int get_Grown_List_Count(tDevice *device, uint32_t *grownCount)
{
    int ret = SUCCESS;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //get from SMART attribute 5 or from device statistics log
        bool grownCountFound = false;
        if (device->drive_info.softSATFlags.deviceStatisticsSupported)
        {
            uint8_t rotatingMediaStatistics[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, rotatingMediaStatistics, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                uint64_t *qWordPtr = (uint64_t*)&rotatingMediaStatistics[0];
                if (qWordPtr[4] & BIT63 && qWordPtr[4] & BIT62)
                {
                    *grownCount = M_DoubleWord0(qWordPtr[4]);
                    grownCountFound = true;
                }
            }
        }
        if (!grownCountFound && is_SMART_Enabled(device))
        {
            smartLogData smartData;
            memset(&smartData, 0, sizeof(smartLogData));
            if (SUCCESS == get_SMART_Attributes(device, &smartData))
            {
                //now get the count from the SMART attribute raw data
                if (smartData.attributes.ataSMARTAttr.attributes[5].valid)
                {
                    *grownCount = M_BytesTo4ByteValue(smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[3], \
                        smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[2], \
                        smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[1], \
                        smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[0]);
                    grownCountFound = true;
                }
            }
        }
        if (!grownCountFound)
        {
            ret = NOT_SUPPORTED;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t defectData[8] = { 0 };
        //get by reading the grown list since it contains a number of entries at the beggining
        if (SUCCESS == scsi_Read_Defect_Data_12(device, false, true, AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR, 0, 8, defectData))//physical chs
        {
            *grownCount = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]) / 8;
        }
        else if (SUCCESS == scsi_Read_Defect_Data_10(device, false, true, AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR, 8, defectData))
        {
            *grownCount = M_BytesTo2ByteValue(defectData[2], defectData[3]) / 8;
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

//there is also a "get" method that should be added below
int sct_Set_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool enableDisable, bool defaultValue, bool isVolatile, uint16_t hdaTemperatureIntervalOrState)
{
    int ret = NOT_SUPPORTED;
    //Note: SCT is a SATA thing. No SCSI equivalent
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check if SCT and SCT feature control is supported
        if (device->drive_info.IdentifyData.ata.Word206 & BIT0 && device->drive_info.IdentifyData.ata.Word206 & BIT4)
        {
            uint16_t featureCode = 0, state = 0, optionFlags = 0;
            switch (sctFeature)
            {
            case SCT_FEATURE_CONTROL_WRITE_CACHE_STATE:
                //set feature code
                featureCode = 1;
                //set state
                if (defaultValue)
                {
                    state = 1;
                }
                else
                {
                    if (enableDisable)
                    {
                        state = 2;
                    }
                    else
                    {
                        state = 3;
                    }
                }
                break;
            case SCT_FEATURE_CONTROL_WRITE_CACHE_REORDERING:
                //set feature code
                featureCode = 2;
                //set state
                if (defaultValue)
                {
                    state = 1;//spec says this is the default value
                }
                else
                {
                    if (enableDisable)
                    {
                        state = 1;
                    }
                    else
                    {
                        state = 2;
                    }
                }
                break;
            case SCT_FEATURE_CONTROL_SET_HDA_TEMPERATURE_INTERVAL:
                //set feature code
                featureCode = 3;
                //set state
                if (defaultValue)
                {
                    //for this we need to read the "sample period" from the SCT data tables command...not supported for now
                    return NOT_SUPPORTED;
                }
                else
                {
                    state = hdaTemperatureIntervalOrState;
                }
                break;
            default:
                featureCode = C_CAST(uint16_t, sctFeature);
                state = hdaTemperatureIntervalOrState;
                break;
            }
            //set option flags
            if (!isVolatile)
            {
                optionFlags = BIT0;
            }
            ret = send_ATA_SCT_Feature_Control(device, 0x0001, featureCode, &state, &optionFlags);
            if (ret == SUCCESS)
            {
                //do we need to check and get specific status?
            }
        }
    }
    return ret;
}

int sct_Get_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool *enableDisable, bool *defaultValue, uint16_t *hdaTemperatureIntervalOrState, uint16_t *featureOptionFlags)
{
    int ret = NOT_SUPPORTED;
    //Note: SCT is a SATA thing. No SCSI equivalent
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check if SCT and SCT feature control is supported
        if (device->drive_info.IdentifyData.ata.Word206 & BIT0 && device->drive_info.IdentifyData.ata.Word206 & BIT4)
        {
            uint16_t featureCode = 0, state = 0, optionFlags = 0;
            switch (sctFeature)
            {
            case SCT_FEATURE_CONTROL_WRITE_CACHE_STATE:
                //set feature code
                featureCode = 1;
                break;
            case SCT_FEATURE_CONTROL_WRITE_CACHE_REORDERING:
                //set feature code
                featureCode = 2;
                break;
            case SCT_FEATURE_CONTROL_SET_HDA_TEMPERATURE_INTERVAL:
                //set feature code
                featureCode = 3;
                break;
            default:
                featureCode = C_CAST(uint16_t, sctFeature);
                break;
            }
            ret = send_ATA_SCT_Feature_Control(device, 0x0002, featureCode, &state, &optionFlags);
            if (ret == SUCCESS)
            {
                if (hdaTemperatureIntervalOrState)
                {
                    *hdaTemperatureIntervalOrState = state;
                }
                if (defaultValue)
                {
                    *defaultValue = false;
                }
                switch (sctFeature)
                {
                case SCT_FEATURE_CONTROL_WRITE_CACHE_STATE:
                    switch (state)
                    {
                    case 0x0001:
                        if (defaultValue)
                        {
                            *defaultValue = true;
                        }
                        break;
                    case 0x0002:
                        if (enableDisable)
                        {
                            *enableDisable = true;
                        }
                        break;
                    case 0x0003:
                        if (enableDisable)
                        {
                            *enableDisable = false;
                        }
                        break;
                    default:
                        //unknown, don't do anything
                        break;
                    }
                    break;
                case SCT_FEATURE_CONTROL_WRITE_CACHE_REORDERING:
                    switch (state)
                    {
                    case 0x0001:
                        if (defaultValue)
                        {
                            *defaultValue = true;
                        }
                        if (enableDisable)
                        {
                            *enableDisable = true;
                        }
                        break;
                    case 0x0002:
                        if (enableDisable)
                        {
                            *enableDisable = false;
                        }
                        break;
                    default:
                        //unknown, don't do anything
                        break;
                    }
                    break;
                case SCT_FEATURE_CONTROL_SET_HDA_TEMPERATURE_INTERVAL:
                    //already set above
                    break;
                default://do nothing
                    break;
                }
                //get option flags if pointer is valid
                if (featureOptionFlags)
                {
                    ret = send_ATA_SCT_Feature_Control(device, 0x0003, featureCode, &state, &optionFlags);
                    *featureOptionFlags = optionFlags;
                }
            }
        }
    }
    return ret;
}

int sct_Set_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t timerValueMilliseconds)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word206 & BIT3)//check that the feature is supported by this drive
        {
            if ((timerValueMilliseconds / 100) > UINT16_MAX)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                //made it this far, so the feature is supported
                switch (ercCommand)
                {
                case SCT_ERC_READ_COMMAND:
                    ret = send_ATA_SCT_Error_Recovery_Control(device, 0x0001, 0x0001, NULL, C_CAST(uint16_t, timerValueMilliseconds / UINT32_C(100)));
                    break;
                case SCT_ERC_WRITE_COMMAND:
                    ret = send_ATA_SCT_Error_Recovery_Control(device, 0x0001, 0x0002, NULL, C_CAST(uint16_t, timerValueMilliseconds / UINT32_C(100)));
                    break;
                default:
                    break;
                }
            }
        }
    }
    return ret;
}

int sct_Get_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t *timerValueMilliseconds)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word206 & BIT3)//check that the feature is supported by this drive
        {
            //made it this far, so the feature is supported
            uint16_t currentTimerValue = 0;
            switch (ercCommand)
            {
            case SCT_ERC_READ_COMMAND:
                ret = send_ATA_SCT_Error_Recovery_Control(device, 0x0002, 0x0001, &currentTimerValue, 0);
                break;
            case SCT_ERC_WRITE_COMMAND:
                ret = send_ATA_SCT_Error_Recovery_Control(device, 0x0002, 0x0002, &currentTimerValue, 0);
                break;
            default:
                break;
            }
            if (ret == SUCCESS)
            {
                *timerValueMilliseconds = C_CAST(uint32_t, currentTimerValue) * UINT32_C(100);
            }
        }
    }
    return ret;
}

int enable_Disable_SMART_Feature(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word082 & BIT0)
        {
            if(enable)
            {
                ret = ata_SMART_Enable_Operations(device);
            }
            else
            {
                ret = ata_SMART_Disable_Operations(device);
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        informationalExceptionsControl control;
        memset(&control, 0, sizeof(informationalExceptionsControl));
        if(SUCCESS == get_SCSI_Informational_Exceptions_Info(device, MPC_CURRENT_VALUES, &control, NULL))
        {
            if (enable)
            {
                control.mrie = 6;//closest to an "enable" that we care about
            }
            else
            {
                control.mrie = 0;//disables smart
            }
            ret = set_SCSI_Informational_Exceptions_Info(device, true, &control);
        }
        else
        {
            ret = NOT_SUPPORTED;//leave as this since the drive doesn't support this mode page
        }
    }
    return ret;
}

int set_MRIE_Mode(tDevice *device, uint8_t mrieMode, bool driveDefault)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        informationalExceptionsControl control;
        memset(&control, 0, sizeof(informationalExceptionsControl));
        uint8_t defaultMode = 6;
        if (driveDefault)
        {
            if (SUCCESS == get_SCSI_Informational_Exceptions_Info(device, MPC_DEFAULT_VALUES, &control, NULL))
            {
                defaultMode = control.mrie;
            }
            else
            {
                return FAILURE;
            }
        }
        if(SUCCESS == get_SCSI_Informational_Exceptions_Info(device, MPC_CURRENT_VALUES, &control, NULL))
        {
            control.mrie = mrieMode;
            if (driveDefault)
            {
                control.mrie = defaultMode;
            }
            ret = set_SCSI_Informational_Exceptions_Info(device, true, &control);
        }
        else
        {
            ret = NOT_SUPPORTED;//leave as this since the drive doesn't support this mode page
        }
    }
    return ret;
}

//always gets the control data. log data is optional
int get_SCSI_Informational_Exceptions_Info(tDevice *device, eScsiModePageControl mpc, ptrInformationalExceptionsControl controlData, ptrInformationalExceptionsLog logData)
{
    int ret = NOT_SUPPORTED;
    if (!controlData)
    {
        return BAD_PARAMETER;
    }
    //if logData is non-null, read the log page...do this first in case a mode select is being performed after this function call!
    if (logData)
    {
        uint8_t *infoLogPage = C_CAST(uint8_t*, calloc_aligned(LP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (infoLogPage)
        {
            if (SUCCESS == scsi_Log_Sense_Cmd(device, true, LPC_CUMULATIVE_VALUES, LP_INFORMATION_EXCEPTIONS, 0, 0, infoLogPage, LP_INFORMATION_EXCEPTIONS_LEN))
            {
                //validate the page code since some SATLs return bad data
                if (M_GETBITRANGE(infoLogPage[0], 5, 0) == 0x2F && infoLogPage[1] == 0 &&
                    M_BytesTo2ByteValue(infoLogPage[4], infoLogPage[5]) == 0 //make sure it's param 0
                   )
                {
                    logData->isValid = true;
                    logData->additionalSenseCode = infoLogPage[8];
                    logData->additionalSenseCodeQualifier = infoLogPage[9];
                    logData->mostRecentTemperatureReading = infoLogPage[10];
                }
            }
            safe_Free_aligned(infoLogPage)
        }
    }
    //read the mode page
    uint8_t *infoControlPage = C_CAST(uint8_t*, calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + MP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (infoControlPage)
    {
        bool gotData = false;
        uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_INFORMATION_EXCEPTIONS_CONTROL, MODE_PARAMETER_HEADER_10_LEN + MP_INFORMATION_EXCEPTIONS_LEN, 0, true, false, mpc, infoControlPage))
        {
            gotData = true;
            controlData->deviceSpecificParameter = infoControlPage[3];
        }
        else if (SUCCESS == scsi_Mode_Sense_6(device, MP_INFORMATION_EXCEPTIONS_CONTROL, MODE_PARAMETER_HEADER_6_LEN + MP_INFORMATION_EXCEPTIONS_LEN, 0, true, mpc, infoControlPage))
        {
            gotData = true;
            headerLength = MODE_PARAMETER_HEADER_6_LEN;
            controlData->sixByteCommandUsed = true;
            controlData->deviceSpecificParameter = infoControlPage[2];
        }
        if (gotData)
        {
            ret = SUCCESS;
            if (M_GETBITRANGE(infoControlPage[headerLength + 0], 5, 0) == 0x1C)//check page code since some SATLs return bad data
            {
                controlData->isValid = true;
                controlData->ps = infoControlPage[headerLength + 0] & BIT7;
                controlData->perf = infoControlPage[headerLength + 2] & BIT7;
                controlData->ebf = infoControlPage[headerLength + 2] & BIT5;
                controlData->ewasc = infoControlPage[headerLength + 2] & BIT4;
                controlData->dexcpt = infoControlPage[headerLength + 2] & BIT3;
                controlData->test = infoControlPage[headerLength + 2] & BIT2;
                controlData->ebackerr = infoControlPage[headerLength + 2] & BIT1;
                controlData->logerr = infoControlPage[headerLength + 2] & BIT0;
                controlData->mrie = M_Nibble0(infoControlPage[headerLength + 3]);
                controlData->intervalTimer = M_BytesTo4ByteValue(infoControlPage[headerLength + 4], infoControlPage[headerLength + 5], infoControlPage[headerLength + 6], infoControlPage[headerLength + 7]);
                controlData->reportCount = M_BytesTo4ByteValue(infoControlPage[headerLength + 8], infoControlPage[headerLength + 9], infoControlPage[headerLength + 10], infoControlPage[headerLength + 11]);
            }
        }
        safe_Free_aligned(infoControlPage)
    }
    return ret;
}

int set_SCSI_Informational_Exceptions_Info(tDevice *device, bool save, ptrInformationalExceptionsControl controlData)
{
    int ret = SUCCESS;
    uint8_t *infoControlPage = C_CAST(uint8_t*, calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + MP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!infoControlPage)
    {
        return MEMORY_FAILURE;
    }
    uint8_t modePageDataOffset = MODE_PARAMETER_HEADER_10_LEN;
    //set up the header first
    if (controlData->sixByteCommandUsed)
    {
        modePageDataOffset = MODE_PARAMETER_HEADER_6_LEN;
        infoControlPage[0] = MP_INFORMATION_EXCEPTIONS_LEN;
        infoControlPage[1] = 0;//medium type
        infoControlPage[2] = controlData->deviceSpecificParameter;
        infoControlPage[3] = 0;//block descriptor length
    }
    else
    {
        infoControlPage[0] = M_Byte1(MP_INFORMATION_EXCEPTIONS_LEN);
        infoControlPage[1] = M_Byte0(MP_INFORMATION_EXCEPTIONS_LEN);
        infoControlPage[2] = 0;//medium type
        infoControlPage[3] = controlData->deviceSpecificParameter;
        infoControlPage[4] = 0;//long lba bit = 0
        infoControlPage[5] = RESERVED;
        infoControlPage[6] = 0;//block descriptor length
        infoControlPage[7] = 0;//block descriptor length
    }
    //now we need to set up the page itself
    infoControlPage[modePageDataOffset + 0] = 0x1C;//page code
    if (controlData->ps)
    {
        infoControlPage[modePageDataOffset + 0] |= BIT7;
    }
    infoControlPage[modePageDataOffset + 1] = 0x0A;//page length
    //lots of bits...
    if (controlData->perf)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT7;
    }
    if (controlData->ebf)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT5;
    }
    if (controlData->ewasc)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT4;
    }
    if (controlData->dexcpt)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT3;
    }
    if (controlData->test)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT2;
    }
    if (controlData->ebackerr)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT1;
    }
    if (controlData->logerr)
    {
        infoControlPage[modePageDataOffset + 2] |= BIT0;
    }
    //set MRIE mode
    infoControlPage[modePageDataOffset + 3] = controlData->mrie;
    //interval timer
    infoControlPage[modePageDataOffset + 4] = M_Byte3(controlData->intervalTimer);
    infoControlPage[modePageDataOffset + 5] = M_Byte2(controlData->intervalTimer);
    infoControlPage[modePageDataOffset + 6] = M_Byte1(controlData->intervalTimer);
    infoControlPage[modePageDataOffset + 7] = M_Byte0(controlData->intervalTimer);
    //report count
    infoControlPage[modePageDataOffset + 8] = M_Byte3(controlData->reportCount);
    infoControlPage[modePageDataOffset + 9] = M_Byte2(controlData->reportCount);
    infoControlPage[modePageDataOffset + 10] = M_Byte1(controlData->reportCount);
    infoControlPage[modePageDataOffset + 11] = M_Byte0(controlData->reportCount);

    if (controlData->sixByteCommandUsed)
    {
        ret = scsi_Mode_Select_6(device, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN, true, save, false, infoControlPage, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN);
    }
    else
    {
        ret = scsi_Mode_Select_10(device, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN, true, save, false, infoControlPage, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN);
    }
    safe_Free_aligned(infoControlPage)
    return ret;
}

int enable_Disable_SMART_Attribute_Autosave(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word082 & BIT0 && device->drive_info.IdentifyData.ata.Word085 & BIT0)
        {
            uint8_t smartData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            //read the data
            ret = ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE);
            if (ret == SUCCESS)
            {
                if (M_BytesTo2ByteValue(smartData[369], smartData[368]) & BIT1)
                {
                    ret = ata_SMART_Attribute_Autosave(device, enable);
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}

int enable_Disable_SMART_Auto_Offline(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word082 & BIT0 && device->drive_info.IdentifyData.ata.Word085 & BIT0)
        {
            uint8_t smartData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            //read the data
            ret = ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE);
            if (ret == SUCCESS)
            {
                if (smartData[367] & BIT1)
                {
                    ret = ata_SMART_Auto_Offline(device, enable);
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}

int get_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo)
{
    int ret = NOT_SUPPORTED;
    if (!smartInfo)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check SMART support and enabled
        if (device->drive_info.IdentifyData.ata.Word082 & BIT0 && device->drive_info.IdentifyData.ata.Word085 & BIT0)
        {
            uint8_t smartData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            //read the data
            ret = ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ret)
            {
                smartInfo->smartVersion = M_BytesTo2ByteValue(smartData[1], smartData[0]);
                //attributes?
                smartInfo->offlineDataCollectionStatus = smartData[362];
                smartInfo->selfTestExecutionStatus = smartData[363];
                smartInfo->timeToCompleteOfflineDataCollection = M_BytesTo2ByteValue(smartData[365], smartData[364]);
                //reserved/vendor specific
                smartInfo->offlineDataCollectionCapability = smartData[367];
                smartInfo->smartCapability = M_BytesTo2ByteValue(smartData[369], smartData[368]);
                smartInfo->errorLoggingCapability = smartData[370];
                smartInfo->vendorSpecific = smartData[371];
                smartInfo->shortSelfTestPollingTime = smartData[372];
                smartInfo->extendedSelfTestPollingTime = smartData[373];
                smartInfo->conveyenceSelfTestPollingTime = smartData[374];
                smartInfo->longExtendedSelfTestPollingTime = M_BytesTo2ByteValue(smartData[376], smartData[375]);
            }
        }
    }
    return ret;
}

int print_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo)
{
    int ret = NOT_SUPPORTED;
    if (!smartInfo)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        printf("\n===SMART Info===\n");
        printf("SMART Version: %"PRIu16"\n", smartInfo->smartVersion);
        //off-line data collection status
        printf("Off-line Data Collection Status: \n\t%"PRIX8 "h - ", smartInfo->offlineDataCollectionStatus);
        bool autoOfflineEnabled = smartInfo->offlineDataCollectionStatus & BIT7;
        switch (smartInfo->offlineDataCollectionStatus)
        {
        case 0:
        case 0x80:
            printf("Off-line Data Collection Never Started");
            break;
        case 2:
        case 0x82:
            printf("Off-line data collection activity was completed without error");
            break;
        case 3:
            printf("Off-line activity in progress");
            break;
        case 4:
        case 0x84:
            printf("Off-line data collection activity was suspended by an interrupting command from host");
            break;
        case 5:
        case 0x85:
            printf("Off-line data collection activity was aborted by an interrupting command from host");
            break;
        case 6:
        case 0x86:
            printf("Off-line data collection activity was aborted by the device with a fatal error");
            break;
        default:
            //vendor specific
            if ((smartInfo->offlineDataCollectionStatus >= 0x40 && smartInfo->offlineDataCollectionStatus <= 0x7F) || (smartInfo->offlineDataCollectionStatus >= 0xC0 /* && smartInfo->offlineDataCollectionStatus <= 0xFF */))
            {
                printf("Vendor Specific");
            }
            else //reserved
            {
                printf("Reserved");
            }
        }
        if (autoOfflineEnabled)
        {
            printf(" (Auto-Off-Line Enabled)");
        }
        printf("\n");
        //self test execution status
        printf("Self Test Execution Status: %02"PRIX8"h\n", smartInfo->selfTestExecutionStatus);
        printf("\tPercent Remaining: %"PRIu32"\n", M_Nibble0(smartInfo->selfTestExecutionStatus) * 10);
        printf("\tStatus: ");
        switch (M_Nibble0(smartInfo->selfTestExecutionStatus))
        {
        case 0:
            printf("Self-test routine completed without error or no self-test status is available");
            break;
        case 1:
            printf("The self-test routine was aborted by the host");
            break;
        case 2:
            printf("The self-test routine was interrupted by the host with a hardware or software reset");
            break;
        case 3:
            printf("A fatal error or unknown test error occurred while the device was executing its self-test routine and the device was unable to complete the self-test routine");
            break;
        case 4:
            printf("The previous self-test completed having a test element that failed and the test element that failed is not known");
            break;
        case 5:
            printf("The previous self-test completed having the electrical element of the test failed");
            break;
        case 6:
            printf("The previous self-test completed having the servo and/or seek test element of the test failed");
            break;
        case 7:
            printf("The previous self-test completed having the read element of the test failed");
            break;
        case 8:
            printf("The previous self-test completed having a test element that failed and the device is suspected of having handling damage");
            break;
        case 0xF:
            printf("Self-test routine in progress");
            break;
        default:
            printf("Reserved");
        }
        printf("\n");
        //off-line data collection capability
        printf("Off-Line Data Collection Capabilities:\n");
        if (smartInfo->offlineDataCollectionCapability & BIT7)
        {
            printf("\tReserved\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT6)
        {
            printf("\tSelective Self Test\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT5)
        {
            printf("\tConveyance Self Test\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT4)
        {
            printf("\tShort & Extended Self Test\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT3)
        {
            printf("\tOff-Line Read Scanning\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT2)
        {
            printf("\tReserved\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT1)
        {
            printf("\tAuto-Off-Line\n");
        }
        if (smartInfo->offlineDataCollectionCapability & BIT0)
        {
            printf("\tExecute Off-Line Immediate\n");
        }
        //smart capabilities
        printf("SMART Capabilities:\n");
        if (smartInfo->smartCapability & BIT1)
        {
            printf("\tAttribute Auto-Save\n");
        }
        if (smartInfo->smartCapability & BIT0)
        {
            printf("\tSMART Data Saved before entering power save mode\n");
        }
        //error logging capability
        printf("Error Logging: ");
        if (smartInfo->errorLoggingCapability & BIT0)
        {
            printf("Supported\n");
        }
        else
        {
            printf("Not Supported\n");
        }
        //time to complete off-line data collection
        printf("Time To Complete Off-Line Data Collection: %0.2f minutes\n", smartInfo->timeToCompleteOfflineDataCollection / 60.0);
        //short self test polling time
        if (smartInfo->offlineDataCollectionCapability & BIT4)
        {
            printf("Short Self Test Polling Time: %"PRIu8" minutes\n", smartInfo->shortSelfTestPollingTime);
            //extended self test polling time
            if (smartInfo->extendedSelfTestPollingTime == 0xFF)
            {
                printf("Extended Self Test Polling Time: %"PRIu16" minutes\n", smartInfo->longExtendedSelfTestPollingTime);
            }
            else
            {
                printf("Extended Self Test Polling Time: %"PRIu8" minutes\n", smartInfo->extendedSelfTestPollingTime);
            }
        }
        //conveyance self test polling time
        if (smartInfo->offlineDataCollectionCapability & BIT5)
        {
            printf("Conveyance Self Test Polling Time: %"PRIu8" minutes\n", smartInfo->conveyenceSelfTestPollingTime);
        }
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)

int nvme_Print_Temp_Statistics(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    int index;
    //uint64_t size = 0; 
    uint32_t temperature = 0, pcbTemp = 0, socTemp = 0, scCurrentTemp = 0, scMaxTemp = 0;
    uint64_t maxTemperature = 0, maxSocTemp = 0;
    nvmeGetLogPageCmdOpts   cmdOpts;
    nvmeSmartLog            smartLog;
    EXTENDED_SMART_INFO_T   extSmartLog;
    nvmeSuperCapDramSmart   scDramSmart;

    if (is_Seagate(device, false))
    {
        //STEP-1 : Get Current Temperature from SMART

        memset(&smartLog, 0, sizeof(nvmeSmartLog));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = C_CAST(uint8_t*, &smartLog);
        cmdOpts.dataLen = sizeof(nvmeSmartLog);
        cmdOpts.lid = 0x02;

        ret = nvme_Get_Log_Page(device, &cmdOpts);

        if(ret == SUCCESS)
        {
            temperature = ((smartLog.temperature[1] << 8) | smartLog.temperature[0]);
            temperature = temperature ? temperature - 273 : 0;
            pcbTemp = smartLog.tempSensor[0];
            pcbTemp = pcbTemp ? pcbTemp - 273 : 0;
            socTemp = smartLog.tempSensor[1];
            socTemp = socTemp ? socTemp - 273 : 0;
            
            printf("%-20s : %" PRIu32 " C\n", "Current Temperature", temperature);
            printf("%-20s : %" PRIu32 " C\n", "Current PCB Temperature", pcbTemp);
            printf("%-20s : %" PRIu32 " C\n", "Current SOC Temperature", socTemp);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error: Could not retrieve Log Page 0x02\n");
            }            
        }

        // STEP-2 : Get Max temperature form Ext SMART-id 194
        memset(&smartLog, 0, sizeof(nvmeSmartLog));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = C_CAST(uint8_t*, &extSmartLog);
        cmdOpts.dataLen = sizeof(EXTENDED_SMART_INFO_T);
        cmdOpts.lid = 0xC4;

        ret = nvme_Get_Log_Page(device, &cmdOpts);

        if(ret == SUCCESS)
        {
            for(index = 0; index < NUMBER_EXTENDED_SMART_ATTRIBUTES; index++) 
            {
                if(extSmartLog.vendorData[index].AttributeNumber == VS_ATTR_ID_MAX_LIFE_TEMPERATURE) 
                {
                    maxTemperature = smart_attribute_vs(extSmartLog.Version, extSmartLog.vendorData[index]);
                    maxTemperature = maxTemperature ? maxTemperature - 273 : 0;

                    printf("%-20s : %" PRIu32 " C\n", "Highest Temperature", C_CAST(uint32_t, maxTemperature));
                }
    
                if(extSmartLog.vendorData[index].AttributeNumber == VS_ATTR_ID_MAX_SOC_LIFE_TEMPERATURE) 
                {
                    maxSocTemp = smart_attribute_vs(extSmartLog.Version, extSmartLog.vendorData[index]);
                    maxSocTemp = maxSocTemp ? maxSocTemp - 273 : 0;

                    printf("%-20s : %" PRIu32 " C\n", "Max SOC Temperature", C_CAST(uint32_t, maxSocTemp));
                }
            }
        }

        // STEP-3 : Get Max temperature form SuperCap DRAM temperature
        memset(&scDramSmart, 0, sizeof(nvmeSuperCapDramSmart));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = C_CAST(uint8_t*, &scDramSmart);
        cmdOpts.dataLen = sizeof(nvmeSuperCapDramSmart);
        cmdOpts.lid = 0xCF;

        ret = nvme_Get_Log_Page(device, &cmdOpts);

        if(ret == SUCCESS)
        {
            scCurrentTemp = scDramSmart.attrScSmart.superCapCurrentTemperature;
            scCurrentTemp = scCurrentTemp ? scCurrentTemp - 273 : 0;
            printf("%-20s : %" PRIu32 " C\n", "Super-cap Current Temperature", scCurrentTemp);      
    
            scMaxTemp = scDramSmart.attrScSmart.superCapMaximumTemperature;
            scMaxTemp = scMaxTemp ? scMaxTemp - 273 : 0;
            printf("%-20s : %" PRIu32 " C\n", "Super-cap Max Temperature", scMaxTemp);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error: Could not retrieve Log Page - SuperCap DRAM\n");
            }
            //exitCode = UTIL_EXIT_OPERATION_FAILURE; //should I fail it completely
        }        
    }

    return ret;
}

#endif

#if !defined (DISABLE_NVME_PASSTHROUGH)

int nvme_Print_PCI_Statistics(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    //uint64_t size = 0; 
    uint32_t correctPcieEc = 0, uncorrectPcieEc = 0;
    nvmeGetLogPageCmdOpts   cmdOpts;
    nvmePcieErrorLogPage    pcieErrorLog;

    if (is_Seagate(device, false))
    {

        memset(&pcieErrorLog, 0, sizeof(nvmePcieErrorLogPage));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = C_CAST(uint8_t*, &pcieErrorLog);
        cmdOpts.dataLen = sizeof(nvmePcieErrorLogPage);
        cmdOpts.lid = 0xCB;

        ret = nvme_Get_Log_Page(device, &cmdOpts);

        if(ret == SUCCESS)
        {
            correctPcieEc = pcieErrorLog.badDllpErrCnt + pcieErrorLog.badTlpErrCnt 
                    + pcieErrorLog.rcvrErrCnt + pcieErrorLog.replayTOErrCnt 
                    + pcieErrorLog.replayNumRolloverErrCnt;
        
            uncorrectPcieEc = pcieErrorLog.fcProtocolErrCnt + pcieErrorLog.dllpProtocolErrCnt 
                    + pcieErrorLog.cmpltnTOErrCnt + pcieErrorLog.rcvrQOverflowErrCnt 
                    + pcieErrorLog.unexpectedCplTlpErrCnt + pcieErrorLog.cplTlpURErrCnt 
                    + pcieErrorLog.cplTlpCAErrCnt + pcieErrorLog.reqCAErrCnt  
                    + pcieErrorLog.reqURErrCnt + pcieErrorLog.ecrcErrCnt 
                    + pcieErrorLog.malformedTlpErrCnt + pcieErrorLog.cplTlpPoisonedErrCnt 
                    + pcieErrorLog.memRdTlpPoisonedErrCnt;
        
            printf("%-45s : %u\n", "PCIe Correctable Error Count", correctPcieEc);
            printf("%-45s : %u\n", "PCIe Un-Correctable Error Count", uncorrectPcieEc); 
            printf("%-45s : %u\n", "Unsupported Request Error Status (URES)", pcieErrorLog.reqURErrCnt);
            printf("%-45s : %u\n", "ECRC Error Status (ECRCES)", pcieErrorLog.ecrcErrCnt);
            printf("%-45s : %u\n", "Malformed TLP Status (MTS)", pcieErrorLog.malformedTlpErrCnt);
            printf("%-45s : %u\n", "Receiver Overflow Status (ROS)", pcieErrorLog.rcvrQOverflowErrCnt);
            printf("%-45s : %u\n", "Unexpected Completion Status(UCS)", pcieErrorLog.unexpectedCplTlpErrCnt);
            printf("%-45s : %u\n", "Completion Timeout Status (CTS)", pcieErrorLog.cmpltnTOErrCnt);
            printf("%-45s : %u\n", "Flow Control Protocol Error Status (FCPES)", pcieErrorLog.fcProtocolErrCnt);
            printf("%-45s : %u\n", "Poisoned TLP Status (PTS)", pcieErrorLog.memRdTlpPoisonedErrCnt);
            printf("%-45s : %u\n", "Data Link Protocol Error Status(DLPES)", pcieErrorLog.dllpProtocolErrCnt);
            printf("%-45s : %u\n", "Replay Timer Timeout Status(RTS)", pcieErrorLog.replayTOErrCnt);
            printf("%-45s : %u\n", "Replay_NUM Rollover Status(RRS)", pcieErrorLog.replayNumRolloverErrCnt);
            printf("%-45s : %u\n", "Bad DLLP Status (BDS)", pcieErrorLog.badDllpErrCnt);
            printf("%-45s : %u\n", "Bad TLP Status (BTS)", pcieErrorLog.badTlpErrCnt);
            printf("%-45s : %u\n", "Receiver Error Status (RES)", pcieErrorLog.rcvrErrCnt);
            printf("%-45s : %u\n", "Cpl TLP Unsupported Request Error Count", pcieErrorLog.cplTlpURErrCnt);
            printf("%-45s : %u\n", "Cpl TLP Completion Abort Error Count", pcieErrorLog.cplTlpCAErrCnt);
            printf("%-45s : %u\n", "Cpl TLP Poisoned Error Count", pcieErrorLog.cplTlpPoisonedErrCnt);
            printf("%-45s : %u\n", "Request Completion Abort Error Count", pcieErrorLog.reqCAErrCnt);
            printf("%-45s : %s\n", "Advisory Non-Fatal Error Status(ANFES)", "Not Supported");
            printf("%-45s : %s\n", "Completer Abort Status (CAS)", "Not Supported");

        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error: Could not retrieve Log Page 0x02\n");
            }            
        }
    }

    return ret;
}

#endif

#define SUMMARY_SMART_ERROR_LOG_ENTRY_SIZE UINT8_C(90)
#define SUMMARY_SMART_ERROR_LOG_COMMAND_SIZE UINT8_C(12)
#define SUMMARY_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE UINT8_C(5)

int get_ATA_Summary_SMART_Error_Log(tDevice * device, ptrSummarySMARTErrorLog smartErrorLog)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (!smartErrorLog)
        {
            return BAD_PARAMETER;
        }
        if (is_SMART_Enabled(device))//must be enabled to read this page
        {
            if (device->drive_info.IdentifyData.ata.Word084 & BIT0 || device->drive_info.IdentifyData.ata.Word087 & BIT0)//checking that SMART error logging is supported
            {
                //Check to make sure it is in the SMART log directory
                uint32_t smartErrorLogSize = 0;
                get_ATA_Log_Size(device, ATA_LOG_SUMMARY_SMART_ERROR_LOG, &smartErrorLogSize, false, true);
                if (smartErrorLogSize > 0)
                {
                    uint8_t errorLog[512] = { 0 }; //This log is only 1 page in spec
                    int getLog = ata_SMART_Read_Log(device, ATA_LOG_SUMMARY_SMART_ERROR_LOG, errorLog, 512);
                    if (SUCCESS == getLog || WARN_INVALID_CHECKSUM == getLog)
                    {
                        uint8_t errorLogIndex = errorLog[1];
                        smartErrorLog->version = errorLog[0];
                        if (getLog == SUCCESS)
                        {
                            smartErrorLog->checksumsValid = true;
                        }
                        else
                        {
                            smartErrorLog->checksumsValid = false;
                        }
                        smartErrorLog->deviceErrorCount = M_BytesTo2ByteValue(errorLog[453], errorLog[452]);
                        if (errorLogIndex > 0 && errorLogIndex < SUMMARY_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE)
                        {
                            uint8_t zeros[SUMMARY_SMART_ERROR_LOG_ENTRY_SIZE] = { 0 };
                            uint32_t offset = 2 + ((errorLogIndex - 1) * SUMMARY_SMART_ERROR_LOG_ENTRY_SIZE);//first entry is at offset 2, each entry is 90 bytes long
                            //offset should now be our starting point to populate the list
                            uint16_t entryCount = 0;
                            while(entryCount < SUMMARY_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE && entryCount < smartErrorLog->deviceErrorCount)
                            {
                                //check if the entry is empty
                                if (memcmp(&errorLog[offset], zeros, SUMMARY_SMART_ERROR_LOG_ENTRY_SIZE) == 0)
                                {
                                    //restart the loop to find another entry (if any)
                                    continue;
                                }
                                //each entry has 5 command data structures to fill in followed by error data
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extDataStructures = false;
                                //NOTE: don't memcpy since we aren't packing the structs
                                uint32_t commandEntryOffset = offset;
                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += SUMMARY_SMART_ERROR_LOG_COMMAND_SIZE)
                                {
                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, SUMMARY_SMART_ERROR_LOG_COMMAND_SIZE) == 0)
                                    {
                                        continue;
                                    }
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].transportSpecific = errorLog[commandEntryOffset + 0];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].feature = errorLog[commandEntryOffset + 1];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].count = errorLog[commandEntryOffset + 2];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaLow = errorLog[commandEntryOffset + 3];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaMid = errorLog[commandEntryOffset + 4];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaHi = errorLog[commandEntryOffset + 5];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].device = errorLog[commandEntryOffset + 6];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].contentWritten = errorLog[commandEntryOffset + 7];
                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].timestampMilliseconds = M_BytesTo4ByteValue(errorLog[commandEntryOffset + 11], errorLog[commandEntryOffset + 10], errorLog[commandEntryOffset + 9], errorLog[commandEntryOffset + 8]);
                                    ++(smartErrorLog->smartError[smartErrorLog->numberOfEntries].numberOfCommands);
                                }
                                //now set the error data
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.reserved = errorLog[offset + 60];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.error = errorLog[offset + 61];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.count = errorLog[offset + 62];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaLow = errorLog[offset + 63];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaMid = errorLog[offset + 64];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaHi = errorLog[offset + 65];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.device = errorLog[offset + 66];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.status = errorLog[offset + 67];
                                memcpy(smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.extendedErrorInformation, &errorLog[offset + 68], 19);
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.state = errorLog[offset + 87];
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lifeTimestamp = M_BytesTo2ByteValue(errorLog[offset + 89], errorLog[offset + 88]);
                                ++(smartErrorLog->numberOfEntries);
                                ++entryCount;
                                if (offset >= 92)//second entry or higher
                                {
                                    offset -= SUMMARY_SMART_ERROR_LOG_ENTRY_SIZE;
                                }
                                else //we must be at the 1st entry, so we need to reset to the end
                                {
                                    offset = 362;//final entry in the log
                                }
                            }
                        }
                        else
                        {
                            //nothing to do since index zero means no entries in the list;
                            smartErrorLog->numberOfEntries = 0;
                        }
                        ret = SUCCESS;
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
            }
        }
    }
    return ret;
}

#define EXT_COMP_SMART_ERROR_LOG_ENTRY_SIZE UINT8_C(124)
#define EXT_COMP_SMART_ERROR_LOG_COMMAND_SIZE UINT8_C(18)
#define EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE UINT8_C(4)

#define COMP_SMART_ERROR_LOG_ENTRY_SIZE UINT8_C(90)
#define COMP_SMART_ERROR_LOG_COMMAND_SIZE UINT8_C(12)
#define COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE UINT8_C(5)

//This function will automatically select SMART vs GPL log
int get_ATA_Comprehensive_SMART_Error_Log(tDevice * device, ptrComprehensiveSMARTErrorLog smartErrorLog, bool forceSMARTLog)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (!smartErrorLog)
        {
            return BAD_PARAMETER;
        }
        if (is_SMART_Enabled(device))//must be enabled to read this page
        {
            if (device->drive_info.IdentifyData.ata.Word084 & BIT0 || device->drive_info.IdentifyData.ata.Word087 & BIT0)//checking that SMART error logging is supported
            {
                uint32_t compErrLogSize = 0;
                //now check for GPL summort so we know if we are reading the ext log or not
                if (device->drive_info.ata_Options.generalPurposeLoggingSupported && !forceSMARTLog && SUCCESS == get_ATA_Log_Size(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, &compErrLogSize, true, false) && compErrLogSize > 0)
                {
                    //extended comprehensive SMART error log
                    //We will read each sector of the log as we need it to help with some USB compatibility (and so we don't read more than we need)
                    uint8_t errorLog[512] = { 0 };
                    uint16_t pageNumber = 0;
                    get_ATA_Log_Size(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, &compErrLogSize, true, false);
                    uint16_t maxPage = C_CAST(uint16_t, compErrLogSize / UINT16_C(512));
                    uint16_t pageIter = 0;
                    if (compErrLogSize > 0)
                    {
                        ret = SUCCESS;
                        int getLog = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, pageNumber, errorLog, 512, 0);
                        if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                        {
                            smartErrorLog->version = errorLog[0];
                            if (getLog == SUCCESS)
                            {
                                smartErrorLog->checksumsValid = true;
                            }
                            else
                            {
                                smartErrorLog->checksumsValid = false;
                            }
                            smartErrorLog->extLog = true;
                            smartErrorLog->deviceErrorCount = M_BytesTo2ByteValue(errorLog[501], errorLog[500]);
                            uint16_t errorLogIndex = M_BytesTo2ByteValue(errorLog[3], errorLog[2]);
                            if (errorLogIndex > 0)
                            {
                                //get the starting page number
                                uint8_t pageEntryNumber = errorLogIndex % EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE - 1;//which entry on the page (zero indexed)
                                uint8_t zeros[EXT_COMP_SMART_ERROR_LOG_ENTRY_SIZE] = { 0 };
                                pageNumber = errorLogIndex / EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE;//4 entries per page
                                while (smartErrorLog->numberOfEntries < SMART_EXT_COMPREHENSIVE_ERRORS_MAX && smartErrorLog->numberOfEntries < smartErrorLog->deviceErrorCount && smartErrorLog->numberOfEntries < (UINT8_C(4) * maxPage)/*make sure we don't go beyond the number of pages the drive actually has*/)
                                {
                                    while (pageIter <= maxPage)
                                    {
                                        //first read this page
                                        memset(errorLog, 0, 512);
                                        getLog = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, pageNumber, errorLog, 512, 0);
                                        if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                                        {
                                            uint8_t pageEntryCounter = 0;
                                            if (getLog == WARN_INVALID_CHECKSUM)
                                            {
                                                smartErrorLog->checksumsValid = false;
                                            }
                                            while (pageEntryNumber < EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE && pageEntryCounter < EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE && smartErrorLog->numberOfEntries < (UINT8_C(4) * maxPage)/*make sure we don't go beyond the number of pages the drive actually has*/)//4 entries per page in the ext log
                                            {
                                                uint32_t offset = (pageEntryNumber * EXT_COMP_SMART_ERROR_LOG_ENTRY_SIZE) + EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE;
                                                --pageEntryNumber;//decrement now before we forget. This is so that we roll backwards since this log appends. If this rolls over to UINT8_MAX, we'll break this loop and read another page.
                                                //check if the entry is empty
                                                if (memcmp(&errorLog[offset], zeros, EXT_COMP_SMART_ERROR_LOG_ENTRY_SIZE) == 0)
                                                {
                                                    //restart the loop to find another entry (if any)
                                                    continue;
                                                }
                                                //each entry has 5 command data structures to fill in followed by error data
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extDataStructures = true;
                                                //NOTE: don't memcpy since we aren't packing the structs
                                                uint32_t commandEntryOffset = offset;
                                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += EXT_COMP_SMART_ERROR_LOG_COMMAND_SIZE)
                                                {
                                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, EXT_COMP_SMART_ERROR_LOG_COMMAND_SIZE) == 0)
                                                    {
                                                        continue;
                                                    }
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].deviceControl = errorLog[commandEntryOffset + 0];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].feature = errorLog[commandEntryOffset + 1];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].featureExt = errorLog[commandEntryOffset + 2];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].count = errorLog[commandEntryOffset + 3];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].countExt = errorLog[commandEntryOffset + 4];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaLow = errorLog[commandEntryOffset + 5];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaLowExt = errorLog[commandEntryOffset + 6];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaMid = errorLog[commandEntryOffset + 7];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaMidExt = errorLog[commandEntryOffset + 8];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaHi = errorLog[commandEntryOffset + 9];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaHiExt = errorLog[commandEntryOffset + 10];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].device = errorLog[commandEntryOffset + 11];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].contentWritten = errorLog[commandEntryOffset + 12];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].reserved = errorLog[commandEntryOffset + 13];
                                                    smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].timestampMilliseconds = M_BytesTo4ByteValue(errorLog[commandEntryOffset + 17], errorLog[commandEntryOffset + 16], errorLog[commandEntryOffset + 15], errorLog[commandEntryOffset + 14]);
                                                    ++(smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].numberOfCommands);
                                                }
                                                //now set the error data
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.transportSpecific = errorLog[offset + 90];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.error = errorLog[offset + 91];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.count = errorLog[offset + 92];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.countExt = errorLog[offset + 93];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaLow = errorLog[offset + 94];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaLowExt = errorLog[offset + 95];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaMid = errorLog[offset + 96];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaMidExt = errorLog[offset + 97];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaHi = errorLog[offset + 98];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lbaHiExt = errorLog[offset + 99];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.device = errorLog[offset + 100];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.status = errorLog[offset + 101];
                                                memcpy(smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.extendedErrorInformation, &errorLog[offset + 102], 19);
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.state = errorLog[offset + 121];
                                                smartErrorLog->extSmartError[smartErrorLog->numberOfEntries].extError.lifeTimestamp = M_BytesTo2ByteValue(errorLog[offset + 123], errorLog[offset + 122]);
                                                ++(smartErrorLog->numberOfEntries);
                                                ++pageEntryCounter;
                                            }
                                            //reset the pageEntry number to 4 for next page (start at the end and work backwards)
                                            pageEntryNumber = EXT_COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE - 1;
                                        }
                                        else
                                        {
                                            //break out of the loop!
                                        }
                                        ++pageIter;
                                        if (pageNumber > 0)
                                        {
                                            --pageNumber;//go to the previous page of the log to read entries in order
                                        }
                                        else
                                        {
                                            pageNumber = maxPage - 1;//minus 1 because zero indexed
                                        }
                                    }
                                }
                            }
                            else
                            {
                                smartErrorLog->numberOfEntries = 0;
                                ret = SUCCESS;
                            }
                        }
                        else
                        {
                            ret = FAILURE;
                        }
                    }
                }
                else //GPL log was not available or did not read correctly.
                {
                    //comprehensive SMART error log
                    //read the first sector to get index and device error count. Will read the full thing if those are non-zero
                    get_ATA_Log_Size(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, &compErrLogSize, false, true);
                    if (compErrLogSize > 0)
                    {
                        ret = SUCCESS;
                        uint8_t *errorLog = C_CAST(uint8_t*, calloc_aligned(512, sizeof(uint8_t), device->os_info.minimumAlignment));
                        if (!errorLog)
                        {
                            return MEMORY_FAILURE;
                        }
                        int getLog = ata_SMART_Read_Log(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, errorLog, 512);
                        if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                        {
                            smartErrorLog->version = errorLog[0];
                            if (getLog == SUCCESS)
                            {
                                smartErrorLog->checksumsValid = true;
                            }
                            else
                            {
                                smartErrorLog->checksumsValid = false;
                            }
                            smartErrorLog->deviceErrorCount = M_BytesTo2ByteValue(errorLog[453], errorLog[452]);
                            uint8_t errorLogIndex = errorLog[1];
                            if (errorLogIndex > 0)
                            {
                                //read the full log to populate all fields
                                uint8_t *temp = C_CAST(uint8_t*, realloc_aligned(errorLog, 512, compErrLogSize * sizeof(uint8_t), device->os_info.minimumAlignment));
                                if (!temp)
                                {
                                    safe_Free_aligned(errorLog)
                                    return MEMORY_FAILURE;
                                }
                                errorLog = temp;
                                memset(errorLog, 0, compErrLogSize);
                                getLog = ata_SMART_Read_Log(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, errorLog, compErrLogSize);
                                if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                                {
                                    //We now have the full log in memory. 
                                    //First, figure out the first page to read. Next: need to handle switching between pages as we fill in the structure with data.
                                    uint16_t pageNumber = errorLogIndex / COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE;//5 entries per page
                                    uint16_t maxPages = C_CAST(uint16_t, compErrLogSize / UINT16_C(512));
                                    uint16_t pageIter = 0;
                                    //byte offset, this will point to the first entry
                                    uint8_t pageEntryNumber = errorLogIndex % COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE - 1;//remainder...zero indexed
                                    uint32_t offset = 0;// (pageNumber * 512) + (pageEntryNumber * 90) + 2;
                                    //EX: Entry 28: pageNumber = 28 / 5 = 5;
                                    //              pageEntryNumber = 28 % 5 = 3;
                                    //              offset = (5 * 512) + (3 * 90) + 2;
                                    //              5 * 512 gets us to that page offset (2560)
                                    //              3 * 90 + 2 gets us to the entry offset on the page we need = 272, which is 4th entry on the page (5th page)
                                    //              this gets us entry 4 on page 5 which is entry number 28
                                    //Now, we need to loop through the data and jump between pages.
                                    //go until we fill up our structure with a max number of entries
                                    uint8_t zeros[COMP_SMART_ERROR_LOG_ENTRY_SIZE] = { 0 };
                                    while (smartErrorLog->numberOfEntries < SMART_COMPREHENSIVE_ERRORS_MAX && smartErrorLog->numberOfEntries < smartErrorLog->deviceErrorCount  && smartErrorLog->numberOfEntries < (UINT8_C(5) * maxPages)/*make sure we don't go beyond the number of pages the drive actually has*/)
                                    {
                                        while (pageIter <= maxPages)
                                        {
                                            uint16_t pageEntryCounter = 0;
                                            while (pageEntryNumber < COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE && pageEntryCounter < COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE && smartErrorLog->numberOfEntries < (UINT8_C(5) * maxPages)/*make sure we don't go beyond the number of pages the drive actually has*/)
                                            {
                                                //calculate the offset of the first entry we need to read from this page
                                                offset = (pageNumber * 512) + (pageEntryNumber * COMP_SMART_ERROR_LOG_ENTRY_SIZE) + 2;
                                                --pageEntryNumber;//decrement now before we forget. This is so that we roll backwards since this log appends. If this rolls over to UINT8_MAX, we'll break this loop and read another page.
                                                //read the entry into memory if it is valid, otherwise continue the loop
                                                //check if the entry is empty
                                                if (memcmp(&errorLog[offset], zeros, COMP_SMART_ERROR_LOG_ENTRY_SIZE) == 0)
                                                {
                                                    //restart the loop to find another entry (if any)
                                                    continue;
                                                }
                                                //each entry has 5 command data structures to fill in followed by error data
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extDataStructures = false;
                                                //NOTE: don't memcpy since we aren't packing the structs
                                                uint32_t commandEntryOffset = offset;
                                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += COMP_SMART_ERROR_LOG_COMMAND_SIZE)
                                                {
                                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, COMP_SMART_ERROR_LOG_COMMAND_SIZE) == 0)
                                                    {
                                                        continue;
                                                    }
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].transportSpecific = errorLog[commandEntryOffset + 0];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].feature = errorLog[commandEntryOffset + 1];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].count = errorLog[commandEntryOffset + 2];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaLow = errorLog[commandEntryOffset + 3];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaMid = errorLog[commandEntryOffset + 4];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].lbaHi = errorLog[commandEntryOffset + 5];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].device = errorLog[commandEntryOffset + 6];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].contentWritten = errorLog[commandEntryOffset + 7];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].command[commandEntry].timestampMilliseconds = M_BytesTo4ByteValue(errorLog[commandEntryOffset + 11], errorLog[commandEntryOffset + 10], errorLog[commandEntryOffset + 9], errorLog[commandEntryOffset + 8]);
                                                    ++(smartErrorLog->smartError[smartErrorLog->numberOfEntries].numberOfCommands);
                                                }
                                                //now set the error data
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.reserved = errorLog[offset + 60];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.error = errorLog[offset + 61];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.count = errorLog[offset + 62];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaLow = errorLog[offset + 63];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaMid = errorLog[offset + 64];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lbaHi = errorLog[offset + 65];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.device = errorLog[offset + 66];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.status = errorLog[offset + 67];
                                                memcpy(smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.extendedErrorInformation, &errorLog[offset + 68], 19);
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.state = errorLog[offset + 87];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].error.lifeTimestamp = M_BytesTo2ByteValue(errorLog[offset + 89], errorLog[offset + 88]);
                                                ++(smartErrorLog->numberOfEntries);
                                                ++pageEntryCounter;
                                            }
                                            pageEntryNumber = COMP_SMART_ERROR_LOG_MAX_ENTRIES_PER_PAGE - 1;//back to last entry for the previous page
                                            ++pageIter;
                                            if (pageNumber > 0)
                                            {
                                                --pageNumber;//go to the previous page of the log to read entries in order
                                            }
                                            else
                                            {
                                                pageNumber = maxPages - 1;//minus 1 because zero indexed
                                            }
                                        }
                                    }
                                    ret = SUCCESS;
                                }
                                else
                                {
                                    ret = FAILURE;
                                }
                            }
                            else
                            {
                                smartErrorLog->numberOfEntries = 0;
                                ret = SUCCESS;
                            }
                        }
                        else
                        {
                            ret = FAILURE;
                        }
                        safe_Free_aligned(errorLog)
                    }
                }
            }
        }
    }
    return ret;
}

#define ATA_COMMAND_INFO_MAX_LENGTH UINT8_C(4096) //making this bigger than we need for the moment
//only to be used for the commands defined in the switch! Other commands are not supported by this function!
void get_Read_Write_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    bool isLBAMode = (device & LBA_MODE_BIT);//almost everything should be LBA mode. Only a few CHS things should be here, but we need to handle them
    bool ext = false;//48bit command
    bool async = false;//dma queued and fpdma
    bool stream = false;//read/write stream
    bool streamDir = false;//true = write
    //bool longCmd = false;//read/write long
    bool fpdma = false;
    //bool noRetries = false;
    //uint64_t commandLBA = lba;
    uint32_t sectorsToTransfer = count;//true for synchronous commands...
    switch (commandOpCode)
    {
    case ATA_WRITE_LONG_NORETRY:
    case ATA_READ_LONG_NORETRY:
        //noRetries = true;
    case ATA_READ_LONG_RETRY:
    case ATA_WRITE_LONG_RETRY:
        //longCmd = true;
        break;
    case ATA_READ_SECT_NORETRY:
    case ATA_WRITE_SECT_NORETRY:
    case ATA_READ_DMA_NORETRY:
    case ATA_WRITE_DMA_NORETRY:
        //noRetries = true;
    case ATA_READ_SECT:
    case ATA_WRITE_SECT:
    case ATA_WRITE_SECTV_RETRY:
    case ATA_READ_MULTIPLE:
    case ATA_WRITE_MULTIPLE:
    case ATA_READ_DMA_RETRY:
    case ATA_WRITE_DMA_RETRY:
        break;
    case ATA_READ_SECT_EXT:
    case ATA_READ_DMA_EXT:
    case ATA_READ_READ_MULTIPLE_EXT:
    case ATA_WRITE_MULTIPLE_FUA_EXT:
    case ATA_WRITE_SECT_EXT:
    case ATA_WRITE_DMA_EXT:
    case ATA_WRITE_MULTIPLE_EXT:
    case ATA_WRITE_DMA_FUA_EXT:
        ext = true;
        break;
    case ATA_WRITE_STREAM_DMA_EXT:
    case ATA_WRITE_STREAM_EXT:
        streamDir = true;
        M_FALLTHROUGH
    case ATA_READ_STREAM_DMA_EXT:
    case ATA_READ_STREAM_EXT:
        ext = true;
        stream = true;
        break;
    case ATA_READ_VERIFY_NORETRY:
        //noRetries = true;
    case ATA_READ_VERIFY_RETRY:
        break;
    case ATA_READ_VERIFY_EXT:
        ext = true;
        break;
    case ATA_READ_FPDMA_QUEUED_CMD:
    case ATA_WRITE_FPDMA_QUEUED_CMD:
        fpdma = true;
        M_FALLTHROUGH
    case ATA_READ_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUE_FUA_EXT:
    case ATA_WRITE_DMA_QUE_EXT:
        ext = true;
        M_FALLTHROUGH
    case ATA_WRITE_DMA_QUEUED_CMD:
    case ATA_READ_DMA_QUEUED_CMD:
        async = true;
        sectorsToTransfer = features;//number of sectors to tansfer
        break;
    default://unknown command...
        return;
    }
    if (async)
    {
        //parse out fields we need for command info for asynchronous commands
        if (ext)
        {
            //interpretting all of this as LBA mode since spec requires it
            bool forceUnitAccess = device & BIT7;//fpdma only
            uint8_t prio = M_GETBITRANGE(count, 15, 14);//fpdma only
            uint8_t tag = M_GETBITRANGE(count, 7, 3);
            bool rarc = count & BIT0;//read fpdma only
            if (sectorsToTransfer == 0)
            {
                sectorsToTransfer = 65536;
            }
            if (fpdma)
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 " NCQ Tag: %" PRIu8 " FUA: %d PRIO: %" PRIu8 " RARC: %d", commandName, lba, sectorsToTransfer, tag, forceUnitAccess, prio, rarc);
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 " Tag: %" PRIu8 "", commandName, lba, sectorsToTransfer, tag);
            }
        }
        else //old dma queued commands
        {
            uint8_t tag = M_GETBITRANGE(count, 7, 3);
            if (sectorsToTransfer == 0)
            {
                sectorsToTransfer = 256;
            }
            if (isLBAMode)//probably not necessary since these commands only ever reference LBA mode...
            {
                uint32_t readSecLBA = M_Nibble0(device) << 24;
                readSecLBA |= M_DoubleWord0(lba) & UINT32_C(0x00FFFFFF);//grabbing first 24 bits only since the others should be zero
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu32 " Count: %" PRIu32 " Tag: %" PRIu8 "", commandName, readSecLBA, sectorsToTransfer, tag);
            }
            else
            {
                uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                uint8_t head = M_Nibble0(device);
                uint8_t sector = M_Byte0(lba);
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu32 " Tag: %" PRIu8 "", commandName, cylinder, head, sector, sectorsToTransfer, tag);
            }
        }
    }
    else
    {
        if (ext)
        {
            if (sectorsToTransfer == 0)
            {
                sectorsToTransfer = 65536;
            }
            if (isLBAMode)
            {
                if (stream)
                {
                    uint8_t cctl = M_Byte1(features);
                    bool urgentTransferRequest = features & BIT7;
                    bool readWriteContinuous = features & BIT6;
                    bool notSequentialORFlush = features & BIT5;//not sequential = read; flush = write
                    bool handleStreamingError = features & BIT4;
                    //bool reserved = features & BIT2;
                    uint8_t streamID = M_GETBITRANGE(features, 2, 0);
                    if (streamDir)//true = write
                    {
                        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 " StreamID: %" PRIu8 " CCTL: %" PRIu8 " Urgent: %d WC: %d Flush %d HSE: %d", commandName, lba, sectorsToTransfer, streamID, cctl, urgentTransferRequest, readWriteContinuous, notSequentialORFlush, handleStreamingError);
                    }
                    else
                    {
                        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 " StreamID: %" PRIu8 " CCTL: %" PRIu8 " Urgent: %d RC: %d NC %d HSE: %d", commandName, lba, sectorsToTransfer, streamID, cctl, urgentTransferRequest, readWriteContinuous, notSequentialORFlush, handleStreamingError);
                    }
                }
                else
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 "", commandName, lba, sectorsToTransfer);
                }
            }
            else //unlikely...most or all transfers should be LBA mode for this command...ATA6 does not require LBA mode bit set like later specifications do
            {
                uint32_t cylinder = M_BytesTo4ByteValue(M_Byte5(lba), M_Byte4(lba), M_Byte2(lba), M_Byte1(lba));
                uint8_t head = M_Nibble0(device);
                uint16_t sector = M_BytesTo2ByteValue(M_Byte3(lba), M_Byte0(lba));
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Cylinder: %" PRIu32 " Head: %" PRIu8 " Sector: %" PRIu16 " Count: %" PRIu32 "", commandName, cylinder, head, sector, sectorsToTransfer);
            }
        }
        else
        {
            if (sectorsToTransfer == 0)
            {
                sectorsToTransfer = 256;
            }
            if (isLBAMode)
            {
                uint32_t readSecLBA = M_Nibble0(device) << 24;
                readSecLBA |= M_DoubleWord0(lba) & UINT32_C(0x00FFFFFF);//grabbing first 24 bits only since the others should be zero
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu32 " Count: %" PRIu32 "", commandName, readSecLBA, sectorsToTransfer);
            }
            else
            {
                uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                uint8_t head = M_Nibble0(device);
                uint8_t sector = M_Byte0(lba);
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu32 "", commandName, cylinder, head, sector, sectorsToTransfer);
            }
        }
    }
}

void get_GPL_Log_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint16_t pageNumber = M_BytesTo2ByteValue(M_Byte5(lba), M_Byte1(lba));
    uint8_t logAddress = M_Byte0(lba);
    char logAddressName[32] = { 0 };
    uint32_t logPageCount = count;
    bool invalidLog = false;
    if (commandOpCode == ATA_SEND_FPDMA || commandOpCode == ATA_RECEIVE_FPDMA)//these commands can encapsulate read/write log ext commands
    {
        logPageCount = features;
    }
    if (logPageCount == 0)
    {
        logPageCount = 65536;
    }
    switch (logAddress)
    {
    case ATA_LOG_DIRECTORY:
        snprintf(logAddressName, 31, "Directory");
        break;
    case ATA_LOG_SUMMARY_SMART_ERROR_LOG://smart log...should be an error using this command!
        snprintf(logAddressName, 31, "Summary SMART Error");
        invalidLog = true;
        break;
    case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG://smart log...should be an error using this command!
        snprintf(logAddressName, 31, "Comprehensive SMART Error");
        invalidLog = true;
        break;
    case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG:
        snprintf(logAddressName, 31, "Ext Comprehensive SMART Error");
        break;
    case ATA_LOG_DEVICE_STATISTICS:
        snprintf(logAddressName, 31, "Device Statistics");
        break;
    case ATA_LOG_SMART_SELF_TEST_LOG://smart log...should be an error using this command!
        snprintf(logAddressName, 31, "SMART Self-Test");
        invalidLog = true;
        break;
    case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG:
        snprintf(logAddressName, 31, "Ext SMART Self-Test");
        break;
    case ATA_LOG_POWER_CONDITIONS:
        snprintf(logAddressName, 31, "Power Conditions");
        break;
    case ATA_LOG_SELECTIVE_SELF_TEST_LOG://smart log...should be an error using this command!
        snprintf(logAddressName, 31, "Selective Self-Test");
        invalidLog = true;
        break;
    case ATA_LOG_DEVICE_STATISTICS_NOTIFICATION:
        snprintf(logAddressName, 31, "Device Statistics Notification");
        break;
    case ATA_LOG_PENDING_DEFECTS_LOG:
        snprintf(logAddressName, 31, "Pending Defects");
        break;
    case ATA_LOG_LPS_MISALIGNMENT_LOG:
        snprintf(logAddressName, 31, "LPS Misalignment");
        break;
    case ATA_LOG_SENSE_DATA_FOR_SUCCESSFUL_NCQ_COMMANDS:
        snprintf(logAddressName, 31, "Sense Data for Successful NCQ");
        break;
    case ATA_LOG_NCQ_COMMAND_ERROR_LOG:
        snprintf(logAddressName, 31, "NCQ Command Errors");
        break;
    case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG:
        snprintf(logAddressName, 31, "SATA Phy Event Counters");
        break;
    case ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG:
        snprintf(logAddressName, 31, "NCQ Queue Management");
        break;
    case ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG:
        snprintf(logAddressName, 31, "NCQ Send and Receive");
        break;
    case ATA_LOG_HYBRID_INFORMATION:
        snprintf(logAddressName, 31, "Hybrid Information");
        break;
    case ATA_LOG_REBUILD_ASSIST:
        snprintf(logAddressName, 31, "Rebuild Assist");
        break;
    case ATA_LOG_LBA_STATUS:
        snprintf(logAddressName, 31, "LBA Status");
        break;
    case ATA_LOG_STREAMING_PERFORMANCE:
        snprintf(logAddressName, 31, "Streaming Performance");
        break;
    case ATA_LOG_WRITE_STREAM_ERROR_LOG:
        snprintf(logAddressName, 31, "Write Stream Errors");
        break;
    case ATA_LOG_READ_STREAM_ERROR_LOG:
        snprintf(logAddressName, 31, "Read Stream Errors");
        break;
    case ATA_LOG_DELAYED_LBA_LOG:
        snprintf(logAddressName, 31, "Delayed LBA");
        break;
    case ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG:
        snprintf(logAddressName, 31, "Current Device Internal Status");
        break;
    case ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG:
        snprintf(logAddressName, 31, "Saved Device Internal Status");
        break;
    case ATA_LOG_SECTOR_CONFIGURATION_LOG:
        snprintf(logAddressName, 31, "Sector Configuration");
        break;
    case ATA_LOG_IDENTIFY_DEVICE_DATA:
        snprintf(logAddressName, 31, "Identify Device Data");
        break;
    case ATA_SCT_COMMAND_STATUS:
        snprintf(logAddressName, 31, "SCT Command/Status");
        break;
    case ATA_SCT_DATA_TRANSFER:
        snprintf(logAddressName, 31, "SCT Data Transfer");
        break;
    default:
        if (logAddress >= 0x80 && logAddress <= 0x9F)
        {
            snprintf(logAddressName, 31, "Host Specific (%02" PRIX8 "h)", logAddress);
        }
        else if (logAddress >= 0xA0 && logAddress <= 0xDF)
        {
            snprintf(logAddressName, 31, "Vendor Specific (%02" PRIX8 "h)", logAddress);
        }
        else
        {
            snprintf(logAddressName, 31, "Unknown (%02" PRIX8 "h)", logAddress);
        }
        break;
    }
    if (invalidLog)
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Log: %s (Invalid Address) Page Number: %" PRIu16 " PageCount: %" PRIu32 " Features: %" PRIX16 "h", commandName, logAddressName, pageNumber, logPageCount, features);
    }
    else
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Log: %s Page Number: %" PRIu16 " PageCount: %" PRIu32 " Features: %" PRIX16 "h", commandName, logAddressName, pageNumber, logPageCount, features);
    }
}

void get_Download_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_Byte0(features);
    uint16_t blockCount = M_BytesTo2ByteValue(M_Byte0(lba), M_Byte0(count));
    uint16_t bufferOffset = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
    char subCommandName[21] = { 0 };
    switch (subcommand)
    {
    case 0x01://immediate temporary use (obsolete)
        snprintf(subCommandName, 20, "Temporary");
        break;
    case 0x03://offsets and save immediate
        snprintf(subCommandName, 20, "Offsets - Immediate");
        break;
    case 0x07://save for immediate use (full buffer)
        snprintf(subCommandName, 20, "Full - Immediate");
        break;
    case 0x0E://offsets and defer for future activation
        snprintf(subCommandName, 20, "Offsets - Deferred");
        break;
    case 0x0F://Activate deferred code
        snprintf(subCommandName, 20, "Activate");
        break;
    default://unknown because not yet defined when this was written
        snprintf(subCommandName, 20, "Unknown Mode (%02" PRIX8 "h)", subcommand);
        break;
    }
    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Mode: %s Block Count: %" PRIu16 " Buffer Offset: %" PRIu16 "", commandName, subCommandName, blockCount, bufferOffset);
}

void get_Trusted_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t securityProtocol = M_Byte0(features);
    uint16_t securityProtocolSpecific = M_BytesTo2ByteValue(M_Byte3(lba), M_Byte2(lba));
    uint16_t transferLength = M_BytesTo2ByteValue(M_Byte0(lba), M_Byte0(count));
    char securityProtocolName[31] = { 0 };
    switch (securityProtocol)
    {
    case SECURITY_PROTOCOL_RETURN_SUPPORTED:
        snprintf(securityProtocolName, 30, "Supported");
        break;
    case SECURITY_PROTOCOL_TCG_1:
    case SECURITY_PROTOCOL_TCG_2:
    case SECURITY_PROTOCOL_TCG_3:
    case SECURITY_PROTOCOL_TCG_4:
    case SECURITY_PROTOCOL_TCG_5:
    case SECURITY_PROTOCOL_TCG_6:
        snprintf(securityProtocolName, 30, "TCG %" PRIu8 "", securityProtocol);
        break;
    case SECURITY_PROTOCOL_CbCS:
        snprintf(securityProtocolName, 30, "CbCS");
        break;
    case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
        snprintf(securityProtocolName, 30, "Tape Encryption");
        break;
    case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
        snprintf(securityProtocolName, 30, "Encryption Configuration");
        break;
    case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
        snprintf(securityProtocolName, 30, "SA Creation Cap");
        break;
    case SECURITY_PROTOCOL_IKE_V2_SCSI:
        snprintf(securityProtocolName, 30, "IKE V2 SCSI");
        break;
    case SECURITY_PROTOCOL_NVM_EXPRESS:
        snprintf(securityProtocolName, 30, "NVM Express");
        break;
    case SECURITY_PROTOCOL_SCSA:
        snprintf(securityProtocolName, 30, "SCSA");
        break;
    case SECURITY_PROTOCOL_JEDEC_UFS:
        snprintf(securityProtocolName, 30, "JEDEC UFS");
        break;
    case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
        snprintf(securityProtocolName, 30, "SD Trusted Flash");
        break;
    case SECURITY_PROTOCOL_IEEE_1667:
        snprintf(securityProtocolName, 30, "IEEE 1667");
        break;
    case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
        snprintf(securityProtocolName, 30, "ATA Security");
        break;
    default:
        if (securityProtocol >= 0xF0 /* && securityProtocol <= 0xFF */)
        {
            snprintf(securityProtocolName, 30, "Vendor Specific (%02" PRIX8"h)", securityProtocol);
            break;
        }
        else
        {
            snprintf(securityProtocolName, 30, "Unknown (%02" PRIX8"h)", securityProtocol);
            break;
        }
    }
    if (commandOpCode == ATA_TRUSTED_NON_DATA)
    {
        transferLength = 0;
        if (device & BIT0)//spec is a little misleading, but the bits 24:27 are in the device/head register on 28 bit commands
        {
            //receive
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s (Receive) - Protocol: %s Protocol Specific: %" PRIu16 "", commandName, securityProtocolName, securityProtocolSpecific);
        }
        else
        {
            //send
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s (Send) - Protocol: %s Protocol Specific: %" PRIu16 "", commandName, securityProtocolName, securityProtocolSpecific);
        }
    }
    else
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Protocol: %s Protocol Specific: %" PRIu16 " Transfer Length: %" PRIu16 "", commandName, securityProtocolName, securityProtocolSpecific, transferLength);
    }
}

void get_SMART_Offline_Immediate_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, M_ATTR_UNUSED uint16_t features, M_ATTR_UNUSED uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH], const char *smartSigValid)
{
    uint8_t offlineImmdTest = M_Byte0(lba);
    char offlineTestName[41] = { 0 };
    switch (offlineImmdTest)
    {
    case 0://SMART off-line routine (offline mode)
        snprintf(offlineTestName, 40, "SMART Off-line routine");
        break;
    case 0x01://short self test (offline)
        snprintf(offlineTestName, 40, "Short Self-Test (offline)");
        break;
    case 0x02://extended self test (offline)
        snprintf(offlineTestName, 40, "Extended Self-Test (offline)");
        break;
    case 0x03://conveyance self test (offline)
        snprintf(offlineTestName, 40, "Conveyance Self-Test (offline)");
        break;
    case 0x04://selective self test (offline)
        snprintf(offlineTestName, 40, "Selective Self-Test (offline)");
        break;
    case 0x7F://abort offline test
        snprintf(offlineTestName, 40, "Abort Self-Test");
        break;
    case 0x81://short self test (captive)
        snprintf(offlineTestName, 40, "Short Self-Test (captive)");
        break;
    case 0x82://extended self test (captive)
        snprintf(offlineTestName, 40, "Extended Self-Test (captive)");
        break;
    case 0x83://conveyance self test (captive)
        snprintf(offlineTestName, 40, "Conveyance Self-Test (captive)");
        break;
    case 0x84://selective self test (captive)
        snprintf(offlineTestName, 40, "Selective Self-Test (captive)");
        break;
    default:
        if (offlineImmdTest >= 0x05 && offlineImmdTest <= 0x3F)
        {
            //reserved (offline)
            snprintf(offlineTestName, 40, "Unknown %" PRIX8 "h (offline)", offlineImmdTest);
        }
        else if (offlineImmdTest == 0x80 || (offlineImmdTest >= 0x85 && offlineImmdTest <= 0x8F))
        {
            //reserved (captive)
            snprintf(offlineTestName, 40, "Unknown %" PRIX8 "h (captive)", offlineImmdTest);
        }
        else if (offlineImmdTest >= 0x40 && offlineImmdTest <= 0x7E)
        {
            //vendor unique (offline)
            snprintf(offlineTestName, 40, "Vendor Specific %" PRIX8 "h (offline)", offlineImmdTest);
        }
        else if (offlineImmdTest >= 0x90 /* && offlineImmdTest <= 0xFF*/ )
        {
            //vendor unique (captive)
            snprintf(offlineTestName, 40, "Vendor Specific %" PRIX8 "h (captive)", offlineImmdTest);
        }
        else
        {
            //shouldn't get here, but call it a generic unknown self test
            snprintf(offlineTestName, 40, "Unknown %" PRIX8 "h", offlineImmdTest);
        }
        break;
    }
    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Offline Immediate: %s, SMART Signature %s", commandName, offlineTestName, smartSigValid);
}

void get_SMART_Log_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH], const char *smartSigValid)
{
    uint8_t logAddress = M_Byte0(lba);
    char logAddressName[41] = { 0 };
    uint8_t logPageCount = M_Byte0(count);
    bool invalidLog = false;
    switch (logAddress)
    {
    case ATA_LOG_DIRECTORY:
        snprintf(logAddressName, 40, "Directory");
        break;
    case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
        snprintf(logAddressName, 40, "Summary SMART Error");
        break;
    case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
        snprintf(logAddressName, 40, "Comprehensive SMART Error");
        break;
    case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Ext Comprehensive SMART Error");
        invalidLog = true;
        break;
    case ATA_LOG_DEVICE_STATISTICS:
        snprintf(logAddressName, 40, "Device Statistics");
        break;
    case ATA_LOG_SMART_SELF_TEST_LOG:
        snprintf(logAddressName, 40, "SMART Self-Test");
        break;
    case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Ext SMART Self-Test");
        invalidLog = true;
        break;
    case ATA_LOG_POWER_CONDITIONS://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Power Conditions");
        invalidLog = true;
        break;
    case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
        snprintf(logAddressName, 40, "Selective Self-Test");
        break;
    case ATA_LOG_DEVICE_STATISTICS_NOTIFICATION://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Device Statistics Notification");
        invalidLog = true;
        break;
    case ATA_LOG_PENDING_DEFECTS_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Pending Defects");
        invalidLog = true;
        break;
    case ATA_LOG_LPS_MISALIGNMENT_LOG:
        snprintf(logAddressName, 40, "LPS Misalignment");
        break;
    case ATA_LOG_SENSE_DATA_FOR_SUCCESSFUL_NCQ_COMMANDS://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Sense Data for Successful NCQ");
        invalidLog = true;
        break;
    case ATA_LOG_NCQ_COMMAND_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "NCQ Command Errors");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "SATA Phy Event Counters");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "NCQ Queue Management");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "NCQ Send and Receive");
        invalidLog = true;
        break;
    case ATA_LOG_HYBRID_INFORMATION://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Hybrid Information");
        invalidLog = true;
        break;
    case ATA_LOG_REBUILD_ASSIST://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Rebuild Assist");
        invalidLog = true;
        break;
    case ATA_LOG_LBA_STATUS://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "LBA Status");
        invalidLog = true;
        break;
    case ATA_LOG_STREAMING_PERFORMANCE://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Streaming Performance");
        invalidLog = true;
        break;
    case ATA_LOG_WRITE_STREAM_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Write Stream Errors");
        invalidLog = true;
        break;
    case ATA_LOG_READ_STREAM_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Read Stream Errors");
        invalidLog = true;
        break;
    case ATA_LOG_DELAYED_LBA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Delayed LBA");
        invalidLog = true;
        break;
    case ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Current Device Internal Status");
        invalidLog = true;
        break;
    case ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Saved Device Internal Status");
        invalidLog = true;
        break;
    case ATA_LOG_SECTOR_CONFIGURATION_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 40, "Sector Configuration");
        invalidLog = true;
        break;
    case ATA_LOG_IDENTIFY_DEVICE_DATA:
        snprintf(logAddressName, 40, "Identify Device Data");
        break;
    case ATA_SCT_COMMAND_STATUS:
        snprintf(logAddressName, 40, "SCT Command/Status");
        break;
    case ATA_SCT_DATA_TRANSFER:
        snprintf(logAddressName, 40, "SCT Data Transfer");
        break;
    default:
        if (logAddress >= 0x80 && logAddress <= 0x9F)
        {
            snprintf(logAddressName, 40, "Host Specific (%02" PRIX8 "h)", logAddress);
        }
        else if (logAddress >= 0xA0 && logAddress <= 0xDF)
        {
            snprintf(logAddressName, 40, "Vendor Specific (%02" PRIX8 "h)", logAddress);
        }
        else
        {
            snprintf(logAddressName, 40, "Unknown (%02" PRIX8 "h)", logAddress);
        }
        break;
    }
    if (invalidLog)
    {
        if (M_Byte0(features) == 0xD5)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Read Log - Log: %s (Invalid Address) PageCount: %" PRIu8 ", SMART Signature %s", commandName, logAddressName, logPageCount, smartSigValid);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Write Log - Log: %s (Invalid Address) PageCount: %" PRIu8 ", SMART Signature %s", commandName, logAddressName, logPageCount, smartSigValid);
        }
    }
    else
    {
        if (M_Byte0(features) == 0xD5)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Read Log- Log: %s PageCount: %" PRIu8 ", SMART Signature %s", commandName, logAddressName, logPageCount, smartSigValid);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Write Log- Log: %s PageCount: %" PRIu8 ", SMART Signature %s", commandName, logAddressName, logPageCount, smartSigValid);
        }
    }
}

void get_SMART_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_Byte0(features);
    uint16_t smartSignature = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
    char smartSigValid[11] = { 0 };
    if (smartSignature == 0xC24F)
    {
        snprintf(smartSigValid, 10, "Valid");
    }
    else
    {
        snprintf(smartSigValid, 10, "Invalid");
    }
    switch (subcommand)
    {
    case ATA_SMART_READ_DATA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Read SMART Data, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_RDATTR_THRESH:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Read SMART Threshold Data, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_SW_AUTOSAVE:
        if (M_Byte0(count) == 0xF1)//enable
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Attribute Autosave, SMART Signature %s", commandName, smartSigValid);
        }
        else if (M_Byte0(count) == 0)//disable
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Attribute Autosave, SMART Signature %s", commandName, smartSigValid);
        }
        else //invalid field for this command
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Attribute Autosave request %" PRIX8 "h, SMART Signature %s", commandName, M_Byte0(count), smartSigValid);
        }
        break;
    case ATA_SMART_SAVE_ATTRVALUE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Save Attributes, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_EXEC_OFFLINE_IMM:
        get_SMART_Offline_Immediate_Info(commandName, commandOpCode, features, count, lba, device, commandInfo, C_CAST(const char*, smartSigValid));
        break;
    case ATA_SMART_READ_LOG:
    case ATA_SMART_WRITE_LOG:
        get_SMART_Log_Info(commandName, commandOpCode, features, count, lba, device, commandInfo, C_CAST(const char*, smartSigValid));
        break;
    //case ATA_SMART_WRATTR_THRESH:some things say vendor specific, others say obsolete
    case ATA_SMART_ENABLE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Operations, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_DISABLE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Operations, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_RTSMART:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Return Status, SMART Signature %s", commandName, smartSigValid);
        break;
    case ATA_SMART_AUTO_OFFLINE:
        if (M_Byte0(count) == 0xF8)//enable
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Auto Offline, SMART Signature %s", commandName, smartSigValid);
        }
        else if (M_Byte0(count) == 0)//disable
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Auto Offline, SMART Signature %s", commandName, smartSigValid);
        }
        else //invalid field for this command
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Auto Offline request %" PRIX8 "h, SMART Signature %s", commandName, M_Byte0(count), smartSigValid);
        }
        break;
    default:
        if ((/* subcommand >= 0x00 && */ subcommand <= 0xCF)
            || (subcommand >= 0xDC && subcommand <= 0xDF))
        {
            //reserved
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Command %" PRIX8 "h, LBA Low: %" PRIX8 "h, Device: %" PRIX8 "h SMART Signature %s", commandName, subcommand, M_Byte0(lba), device, smartSigValid);
        }
        else
        {
            //vendor unique
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Vendor Unique %" PRIX8 "h, LBA Low: %" PRIX8 "h, Device: %" PRIX8 "h SMART Signature %s", commandName, subcommand, M_Byte0(lba), device, smartSigValid);
        }
        break;
    }
}

void get_Sanitize_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint16_t subcommand = features;
    uint32_t signature = M_DoubleWord0(lba);//TODO: may need to byte swap this //NOTE: for overwrite, this is the pattern. 47:32 contain a signature
    bool zoneNoReset = M_ToBool(count & BIT15);
    bool invertBetweenPasses = M_ToBool(count & BIT7);//overwrite only
    bool definitiveEndingPattern = M_ToBool(count & BIT6);//overwrite only
    bool failure = M_ToBool(count & BIT4);
    bool clearSanitizeOperationFailed = M_ToBool(count & BIT0);//status only
    uint8_t overwritePasses = M_Nibble0(count);//overwrite only
    uint32_t overwritePattern = M_DoubleWord0(lba);//overwrite only
    uint16_t overwriteSignature = M_Word2(lba);
    char sanitizeSignatureValid[11] = { 0 };
    switch (subcommand)
    {
    case ATA_SANITIZE_STATUS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Status, Clear Failure: %d", commandName, clearSanitizeOperationFailed);
        break;
    case ATA_SANITIZE_CRYPTO_SCRAMBLE:
        if (signature == ATA_SANITIZE_CRYPTO_LBA)
        {
            snprintf(sanitizeSignatureValid, 10, "Valid");
        }
        else
        {
            snprintf(sanitizeSignatureValid, 10, "Invalid");
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Crypto Scramble, ZNR: %d, Failure Mode: %d, Signature %s", commandName, zoneNoReset, failure, sanitizeSignatureValid);
        break;
    case ATA_SANITIZE_BLOCK_ERASE:
        if (signature == ATA_SANITIZE_BLOCK_ERASE_LBA)
        {
            snprintf(sanitizeSignatureValid, 10, "Valid");
        }
        else
        {
            snprintf(sanitizeSignatureValid, 10, "Invalid");
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Block Erase, ZNR: %d, Failure Mode: %d, Signature %s", commandName, zoneNoReset, failure, sanitizeSignatureValid);
        break;
    case ATA_SANITIZE_OVERWRITE_ERASE:
        if (overwriteSignature == ATA_SANITIZE_OVERWRITE_LBA)
        {
            snprintf(sanitizeSignatureValid, 10, "Valid");
        }
        else
        {
            snprintf(sanitizeSignatureValid, 10, "Invalid");
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Overwrite Erase, ZNR: %d, Invert: %d, Definitive Pattern: %d, Failure Mode: %d, Passes: %" PRIu8 ", Pattern: %08" PRIX32 "h, Signature %s", commandName, zoneNoReset, invertBetweenPasses, definitiveEndingPattern, failure, overwritePasses, overwritePattern, sanitizeSignatureValid);
        break;
    case ATA_SANITIZE_FREEZE_LOCK:
        if (signature == ATA_SANITIZE_FREEZE_LOCK_LBA)
        {
            snprintf(sanitizeSignatureValid, 10, "Valid");
        }
        else
        {
            snprintf(sanitizeSignatureValid, 10, "Invalid");
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Freeze Lock, Signature %s", commandName, sanitizeSignatureValid);
        break;
    case ATA_SANITIZE_ANTI_FREEZE_LOCK:
        if (signature == ATA_SANITIZE_ANTI_FREEZE_LOCK_LBA)
        {
            snprintf(sanitizeSignatureValid, 10, "Valid");
        }
        else
        {
            snprintf(sanitizeSignatureValid, 10, "Invalid");
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Anti-Freeze Lock, Signature %s", commandName, sanitizeSignatureValid);
        break;
    default://unknown sanitize operation
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%04" PRIX16 "h), LBA = %012" PRIX64 "h, Count = %04" PRIX16 "h", commandName, subcommand, lba, count);
        break;
    }
}

void get_DCO_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_Byte0(features);
    switch (subcommand)
    {
    case DCO_RESTORE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Restore", commandName);
        break;
    case DCO_FREEZE_LOCK:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Freeze Lock", commandName);
        break;
    case DCO_IDENTIFY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Identify", commandName);
        break;
    case DCO_SET:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set", commandName);
        break;
    case DCO_IDENTIFY_DMA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Identify DMA", commandName);
        break;
    case DCO_SET_DMA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set DMA", commandName);
        break;
    default://reserved
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%02" PRIX8 "h), LBA = %07" PRIX32 "h, Count = %02" PRIX8 "h", commandName, subcommand, C_CAST(uint32_t, lba), C_CAST(uint8_t, count));
        break;
    }
}

void get_Set_Max_Address_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    bool volatileValue = count & BIT0;
    if (commandOpCode == ATA_SET_MAX_EXT)
    {
        //48bit command to set max 48bit LBA
        if (volatileValue)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu64 " (Volatile)", commandName, lba);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu64 "", commandName, lba);
        }
    }
    else
    {
        //28bit command to set max or other things like passwords
        uint8_t subcommand = M_Byte0(features);
        switch (subcommand)
        {
        case HPA_SET_MAX_ADDRESS:
            if (volatileValue)
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu32 " (Volatile)", commandName, C_CAST(uint32_t, lba));
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu32 "", commandName, C_CAST(uint32_t, lba));
            }
            break;
        case HPA_SET_MAX_PASSWORD:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Password", commandName);
            break;
        case HPA_SET_MAX_LOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Lock", commandName);
            break;
        case HPA_SET_MAX_UNLOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unlock", commandName);
            break;
        case HPA_SET_MAX_FREEZE_LOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Freeze Lock", commandName);
            break;
        case HPA_SET_MAX_PASSWORD_DMA:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Password DMA", commandName);
            break;
        case HPA_SET_MAX_UNLOCK_DMA:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unlock DMA", commandName);
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%02" PRIX8 "h), LBA = %07" PRIX32 "h, Count = %02" PRIX8 "h", commandName, subcommand, C_CAST(uint32_t, lba), C_CAST(uint8_t, count));
            break;
        }
    }
}

//Only idle and standby...not the immediate commands!
void get_Idle_Or_Standby_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, M_ATTR_UNUSED uint16_t features, uint16_t count, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t standbyTimerPeriod = M_Byte0(count);
    char standbyTimerPeriodString[31] = { 0 };
    switch (standbyTimerPeriod)
    {
    case 0x00://disabled
        snprintf(standbyTimerPeriodString, 30, "Standby Timer Disabled");
        break;
    case 0xFC://21min
        snprintf(standbyTimerPeriodString, 30, "21 Minutes");
        break;
    case 0xFD://between 8h and 12h
        snprintf(standbyTimerPeriodString, 30, "8 to 12 Hours");
        break;
    case 0xFF://21min 15s
        snprintf(standbyTimerPeriodString, 30, "21 Minutes 15 Seconds");
        break;
    case 0xFE://reserved (fall through)
    default:
        if (standbyTimerPeriod >= 0x01 && standbyTimerPeriod <= 0xF0)
        {
            uint64_t timerInSeconds = standbyTimerPeriod * 5;
            uint8_t minutes = 0, seconds = 0;
            convert_Seconds_To_Displayable_Time(timerInSeconds, NULL, NULL, NULL, &minutes, &seconds);
            if (minutes > 0 && seconds == 0)
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Minutes", minutes);
            }
            else if (minutes > 0)
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Minutes %" PRIu8 " Seconds", minutes, seconds);
            }
            else
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Seconds", seconds);
            }
        }
        else if (standbyTimerPeriod >= 0xF1 && standbyTimerPeriod <= 0xFB)
        {
            uint64_t timerInSeconds = ((standbyTimerPeriod - 240) * 30) * 60;//timer is a minutes value that I'm converting to seconds
            uint8_t minutes = 0, hours = 0;//no seconds since it would always be zero
            convert_Seconds_To_Displayable_Time(timerInSeconds, NULL, NULL, &hours, &minutes, NULL);
            if (hours > 0 && minutes == 0)
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Hours", hours);
            }
            else if (hours > 0)
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Hours %" PRIu8 " Minutes", hours, minutes);
            }
            else
            {
                snprintf(standbyTimerPeriodString, 30, "%" PRIu8 " Minutes", minutes);
            }
        }
        else
        {
            snprintf(standbyTimerPeriodString, 30, "Unknown Timer Value (%02" PRIX8 "h)", standbyTimerPeriod);
        }
        break;
    }
    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Standby Timer Period: %s", commandName, standbyTimerPeriodString);
}

void get_NV_Cache_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint16_t subcommand = features;
    switch (subcommand)
    {
    case NV_SET_NV_CACHE_POWER_MODE:
    {
        uint8_t hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time(count, NULL, NULL, &hours, &minutes, &seconds);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set NV Cache Power Mode. Minimum High-Power Time: %" PRIu8 " hours %" PRIu8 " minutes %" PRIu8 " seconds", commandName, hours, minutes, seconds);
    }
        break;
    case NV_RETURN_FROM_NV_CACHE_POWER_MODE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Return From NV Cache Power Mode", commandName);
        break;
    case NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET:
    {
        uint32_t blockCount = count;
        if (blockCount == 0)
        {
            blockCount = 65536;
        }
        bool populateImmediately = lba & BIT0;
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Add LBAs to NV Cache Pinned Set, Populate Immediately: %d, Count = %" PRIu32 "", commandName, populateImmediately, blockCount);
    }
        break;
    case NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET:
    {
        uint32_t blockCount = count;
        if (blockCount == 0)
        {
            blockCount = 65536;
        }
        bool unpinAll = lba & BIT0;
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Remove LBAs to NV Cache Pinned Set, Unpin All: %d, Count = %" PRIu32 "", commandName, unpinAll, blockCount);
    }
        break;
    case NV_QUERY_NV_CACHE_PINNED_SET:
    {
        uint32_t blockCount = count;
        if (blockCount == 0)
        {
            blockCount = 65536;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Query NV Cache Pinned Set, Starting 512B block: %" PRIu64 ", Count = %" PRIu32 "", commandName, lba, blockCount);
    }
        break;
    case NV_QUERY_NV_CACHE_MISSES:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Query NV Cache Misses", commandName);
        break;
    case NV_FLUSH_NV_CACHE:
    {
        uint32_t minimumBlocksToFlush = M_DoubleWord0(lba);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Flush NV Cache Pinned Set, Min Blocks To Flush = %" PRIu32 "", commandName, minimumBlocksToFlush);
    }
        break;
    case NV_CACHE_ENABLE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable NV Cache", commandName);
        break;
    case NV_CACHE_DISABLE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable NV Cache", commandName);
        break;
    default://unknown or vendor specific
        if (subcommand >= 0x00D0 && subcommand <= 0x00EF)
        {
            //vendor specific
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Vendor Specific (%04" PRIX16 "h), LBA = %012" PRIX64 "h, Count = %04" PRIX16 "h", commandName, subcommand, lba, count);
        }
        else
        {
            //reserved for NV cache feature
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%04" PRIX16 "h), LBA = %012" PRIX64 "h, Count = %04" PRIX16 "h", commandName, subcommand, lba, count);
        }
        break;
    }
}

void get_AMAC_Command_Info(const char* commandName, M_ATTR_UNUSED uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    switch (features)
    {
    case AMAC_GET_NATIVE_MAX_ADDRESS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Get Native Max Address", commandName);
        break;
    case AMAC_SET_ACCESSIBLE_MAX_ADDRESS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Accessible Max Address - LBA: %" PRIu64 "", commandName, lba);
        break;
    case AMAC_FREEZE_ACCESSIBLE_MAX_ADDRESS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Freeze Accessible Max Address", commandName);
        break;
    default://reserved - unknown
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%04" PRIX16 "h), LBA = %012" PRIX64 "h, Count = %04" PRIX16 "h", commandName, features, lba, count);
        break;
    }
}

void get_Zeros_Ext_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    bool trimBit = features & BIT0;
    uint32_t numberOfSectorsToWriteZeros = count;
    if (commandOpCode == ATA_FPDMA_NON_DATA)
    {
        //trim bit is in AUX register, so we cannot see it.
        numberOfSectorsToWriteZeros = M_BytesTo2ByteValue(M_Byte1(features), M_Byte1(count));
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - TRIM: (Unknown), LBA: %" PRIu64 " Count: %" PRIu32 "", commandName, lba, numberOfSectorsToWriteZeros);
    }
    else
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - TRIM: %d, LBA: %" PRIu64 " Count: %" PRIu32 "", commandName, trimBit, lba, numberOfSectorsToWriteZeros);
    }
}

void get_SATA_Feature_Control_Command_Info(const char* commandName, bool enable, uint8_t subcommandCount, uint64_t lba, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    char sataFeatureString[81] = { 0 };
    switch (subcommandCount)
    {
    case SATA_FEATURE_NONZERO_BUFFER_OFFSETS:
        snprintf(sataFeatureString, 80, "Nonzero Buffer Offsets");
        break;
    case SATA_FEATURE_DMA_SETUP_FIS_AUTO_ACTIVATE:
        snprintf(sataFeatureString, 80, "DMA Setup FIS Auto Activation Optimization");
        break;
    case SATA_FEATURE_DEVICE_INITIATED_INTERFACE_POWER_STATE_TRANSITIONS:
        snprintf(sataFeatureString, 80, "Device Initiated Interface Power State Transitions");
        break;
    case SATA_FEATURE_GUARANTEED_IN_ORDER_DATA_DELIVERY:
        snprintf(sataFeatureString, 80, "Guaranteed In Order Data Delivery");
        break;
    case SATA_FEATURE_ASYNCHRONOUS_NOTIFICATION:
        snprintf(sataFeatureString, 80, "Asynchronous Notification");
        break;
    case SATA_FEATURE_SOFTWARE_SETTINGS_PRESERVATION:
        snprintf(sataFeatureString, 80, "Software Settings Preservation");
        break;
    case SATA_FEATURE_DEVICE_AUTOMATIC_PARTIAL_TO_SLUMBER_TRANSITIONS:
        snprintf(sataFeatureString, 80, "Device Automatic Partial To Slumber Transitions");
        break;
    case SATA_FEATURE_ENABLE_HARDWARE_FEATURE_CONTROL:
    {
        char hardwareFeatureName[31] = { 0 };
        uint16_t functionID = M_GETBITRANGE(lba, 15, 0);
        switch (functionID)
        {
        case 0x0001:
            snprintf(hardwareFeatureName, 30, "Direct Head Unload");
            break;
        default:
            if (functionID >= 0xF000 /* && functionID <= 0xFFFF */)
            {
                snprintf(hardwareFeatureName, 30, "Vendor Specific (%04" PRIX16 "h)", functionID);
            }
            else
            {
                snprintf(hardwareFeatureName, 30, "Unknown Function (%04" PRIX16 "h)", functionID);
            }
            break;
        }
        snprintf(sataFeatureString, 80, "Enable Hardware Feature Control - %s", hardwareFeatureName);
    }
    break;
    case SATA_FEATURE_ENABLE_DISABLE_DEVICE_SLEEP:
        snprintf(sataFeatureString, 80, "Device Sleep");
        break;
    case SATA_FEATURE_ENABLE_DISABLE_HYBRID_INFORMATION:
        snprintf(sataFeatureString, 80, "Hybrid Information");
        break;
    case SATA_FEATURE_ENABLE_DISABLE_POWER_DISABLE:
        snprintf(sataFeatureString, 80, "Power Disable");
        break;
    default:
        snprintf(sataFeatureString, 80, "Unknown SATA Feature (%02" PRIX8 "h)", subcommandCount);
        break;
    }
    if (enable)
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable SATA Feature - %s", commandName, sataFeatureString);
    }
    else
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable SATA Feature - %s", commandName, sataFeatureString);
    }
}

void get_Set_Features_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t setFeaturesSubcommand = M_Byte0(features);
    uint8_t subcommandCount = M_Byte0(count);
    if (commandOpCode == ATA_FPDMA_NON_DATA)
    {
        setFeaturesSubcommand = M_Byte1(features);
        subcommandCount = M_Byte1(count);
    }
    switch (setFeaturesSubcommand)
    {
    case SF_ENABLE_8_BIT_DATA_TRANSFERS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable 8-bit Data Transfers", commandName);
        break;
    case SF_ENABLE_VOLITILE_WRITE_CACHE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Volatile Write Cache", commandName);
        break;
    case SF_SET_TRANSFER_MODE:
    {
        uint8_t transferType = M_GETBITRANGE(subcommandCount, 7, 3);
        uint8_t mode = M_GETBITRANGE(subcommandCount, 2, 0);
        char transferMode[31] = { 0 };
        switch (transferType)
        {
        case SF_TRANSFER_MODE_PIO_DEFAULT:
            if (mode == 1)
            {
                snprintf(transferMode, 30, "PIO default - Disable IORDY");
            }
            else
            {
                snprintf(transferMode, 30, "PIO default");
            }
            break;
        case SF_TRANSFER_MODE_FLOW_CONTROL:
            snprintf(transferMode, 30, "PIO Flow Control Mode %" PRIu8 "", mode);
            break;
        case SF_TRANSFER_MODE_SINGLE_WORD_DMA:
            snprintf(transferMode, 30, "SWDMA Mode %" PRIu8 "", mode);
            break;
        case SF_TRANSFER_MODE_MULTI_WORD_DMA:
            snprintf(transferMode, 30, "MWDMA Mode %" PRIu8 "", mode);
            break;
        case SF_TRANSFER_MODE_ULTRA_DMA:
            snprintf(transferMode, 30, "Ultra DMA Mode %" PRIu8 "", mode);
            break;
        case SF_TRANSFER_MODE_RESERVED:
        default:
            snprintf(transferMode, 30, "Unknown %02" PRIX8 "h", subcommandCount);
            break;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Transfer Mode: %s", commandName, transferMode);
    }
        break;
    case SF_ENABLE_ALL_AUTOMATIC_DEFECT_REASSIGNMENT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable All Automatic Defect Reassignment", commandName);
        break;
    case SF_ENABLE_APM_FEATURE:
    {
        uint8_t apmLevel = subcommandCount;
        char apmLevelString[51] = { 0 };
        if (apmLevel == 1)
        {
            snprintf(apmLevelString, 50, "Minimum Power Consumption w/ Standby (%02" PRIX8 "h)", apmLevel);
        }
        else if (apmLevel >= 0x02 && apmLevel <= 0x7F)
        {
            snprintf(apmLevelString, 50, "Intermediate Power Management w/ Standby (%02" PRIX8 "h)", apmLevel);
        }
        else if (apmLevel == 0x80)
        {
            snprintf(apmLevelString, 50, "Minimum Power Consumption w/o Standby (%02" PRIX8 "h)", apmLevel);
        }
        else if (apmLevel >= 0x81 && apmLevel <= 0xFD)
        {
            snprintf(apmLevelString, 50, "Intermediate Power Management w/o Standby (%02" PRIX8 "h)", apmLevel);
        }
        else if (apmLevel == 0xFE)
        {
            snprintf(apmLevelString, 50, "Maximum Performance (%02" PRIX8 "h)", apmLevel);
        }
        else
        {
            snprintf(apmLevelString, 50, "Unknown APM Level (%02" PRIX8 "h)", apmLevel);
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Advanced Power Management - %s", commandName, apmLevelString);
    }
        break;
    case SF_ENABLE_PUIS_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Power Up In Standby (PUIS)", commandName);
        break;
    case SF_PUIS_DEVICE_SPIN_UP:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - PUIS Spin-Up", commandName);
        break;
    case SF_ADDRESS_OFFSET_RESERVED_BOOT_AREA_METHOD_TECH_REPORT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Address Offser Reserved Boot Area Method %02" PRIX8 "h", commandName, setFeaturesSubcommand);
        break;
    case SF_ENABLE_CFA_POWER_MODE1:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable CFA Power Mode 1", commandName);
        break;
    case SF_ENABLE_WRITE_READ_VERIFY_FEATURE:
    {
        uint8_t wrvMode = M_Byte0(lba);
#define WRV_MODE_STRING_LENGTH 41
        char wrvModeString[WRV_MODE_STRING_LENGTH] = { 0 };
        switch (wrvMode)
        {
        case 0x00:
            snprintf(wrvModeString, WRV_MODE_STRING_LENGTH, "Mode 0 (All Sectors)");
            break;
        case 0x01:
            snprintf(wrvModeString, WRV_MODE_STRING_LENGTH, "Mode 1 (1st 65536 Sectors)");
            break;
        case 0x02:
            snprintf(wrvModeString, WRV_MODE_STRING_LENGTH, "Mode 2 (Vendor Specific # of Sectors)");
            break;
        case 0x03:
            snprintf(wrvModeString, WRV_MODE_STRING_LENGTH, "Mode 3 (1st %" PRIu32 " Sectors))", C_CAST(uint32_t, subcommandCount * 1024));
            break;
        default:
            snprintf(wrvModeString, WRV_MODE_STRING_LENGTH, "Unknown WRV Mode (%02" PRIX8 "h)", wrvMode);
            break;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Write-Read-Verify: %s", commandName, wrvModeString);
    }
        break;
    case SF_ENABLE_DEVICE_LIFE_CONTROL:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Device Life Control", commandName);
        break;
    case SF_ENABLE_SATA_FEATURE:
        get_SATA_Feature_Control_Command_Info(commandName, true, subcommandCount, lba, commandInfo);
        break;
    case SF_TLC_SET_CCTL:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - TCL Set CCTL - %" PRIu32 " milliseconds", commandName, C_CAST(uint32_t, subcommandCount * 10));
        break;
    case SF_TCL_SET_ERROR_HANDLING:
        if (subcommandCount == 1)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - TCL Error Handling - Read/Write Continuous", commandName);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - TCL Error Handling - Abort", commandName);
        }
        break;
    case SF_DISABLE_MEDIA_STATUS_NOTIFICATION:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Media Status Notification", commandName);
        break;
    case SF_DISABLE_RETRY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Retries", commandName);
        break;
    case SF_ENABLE_FREE_FALL_CONTROL_FEATURE:
        if (subcommandCount == 0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Free-Fall Control: Vendor Recommended Sensitivity", commandName);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Free-Fall Control - Sensitivity: %02" PRIu8 "h", commandName, subcommandCount);
        }
        break;
    case SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE:
        if (subcommandCount == 0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Vendor Specific", commandName);
        }
        else if (subcommandCount >= 0x01 && subcommandCount <= 0x7F)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Retired (%02" PRIX8 "h)", commandName, subcommandCount);
        }
        else if (subcommandCount == 0x80)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Minimum Acoustic Emanation", commandName);
        }
        else if (subcommandCount >= 0x81 && subcommandCount <= 0xFD)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Intermediate Acoustic Mangement Levels (%02" PRIX8 "h)", commandName, subcommandCount);
        }
        else if (subcommandCount == 0xFE)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Maximum Performance", commandName);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Automatic Acoustic Management - Reserved (%02" PRIX8 "h)", commandName, subcommandCount);
        }
        break;
    case SF_MAXIMUM_HOST_INTERFACE_SECTOR_TIMES:
    {
        uint16_t typicalPIOTime = M_BytesTo2ByteValue(M_Byte0(lba), M_Byte0(count));
        uint8_t typicalDMATime = M_Byte1(lba);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Maximum Host Interface Sector Times - PIO: %" PRIu16 " DMA: %" PRIu8 "", commandName, typicalPIOTime, typicalDMATime);
    }
        break;
    case SF_LEGACY_SET_VENDOR_SPECIFIC_ECC_BYTES_FOR_READ_WRITE_LONG:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Vendor Specific ECC Data For Read/Write Long: %" PRIu8 " Bytes", commandName, subcommandCount);
        break;
    case SF_SET_RATE_BASIS:
        switch (subcommandCount)
        {
        case 0x00:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Rate Basis - Time Of Manufacture Until Time Indicated by Date and Time Timestamp", commandName);
            break;
        case 0x04:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Rate Basis - Time Elapsed Since Most Recent Power On Reset", commandName);
            break;
        case 0x08:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Rate Basis - Time Indicated By Power On Hours Device Statistic", commandName);
            break;
        case 0x0F:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Rate Basis - Undetermined", commandName);
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Rate Basis - Unknown(%02" PRIX8 "h)", commandName, subcommandCount);
            break;
        }
        break;
    case SF_EXTENDED_POWER_CONDITIONS:
    {
        uint8_t subcommand = M_GETBITRANGE(lba, 3, 0);
        uint8_t powerConditionCode = subcommandCount;
        uint32_t epcLBA = C_CAST(uint32_t, lba);
        if (commandOpCode == ATA_SET_FEATURE)
        {
            epcLBA = C_CAST(uint32_t, (lba & MAX_28_BIT_LBA)) | (M_Nibble0(device) << 24);
        }
#define POWER_CONDITION_STRING_LENGTH 31
        char powerConditionString[POWER_CONDITION_STRING_LENGTH] = { 0 };
        switch (powerConditionCode)
        {
        case PWR_CND_STANDBY_Z:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Standby_Z");
            break;
        case PWR_CND_STANDBY_Y:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Standby_Y");
            break;
        case PWR_CND_IDLE_A:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Idle_A");
            break;
        case PWR_CND_IDLE_B:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Idle_B");
            break;
        case PWR_CND_IDLE_C:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Idle_C");
            break;
        case PWR_CND_ALL:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "All Supported");
            break;
        default:
            snprintf(powerConditionString, POWER_CONDITION_STRING_LENGTH, "Unknown Pwr Cond (%02" PRIX8 "h)", powerConditionCode);
            break;
        }
        switch (subcommand)
        {
        case EPC_RESTORE_POWER_CONDITION_SETTINGS:
        {
            bool defaultBit = epcLBA & BIT6;
            bool saveBit = epcLBA & BIT4;
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Restore Power Condition Settings - %s Default: %d Save: %d", commandName, powerConditionString, defaultBit, saveBit);
        }
            break;
        case EPC_GO_TO_POWER_CONDITION:
        {
            bool delayedEntry = epcLBA & BIT25;
            bool holdPowerCondition = epcLBA & BIT24;
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Go To Power Condition - %s Delay: %d Hold: %d", commandName, powerConditionString, delayedEntry, holdPowerCondition);
        }
            break;
        case EPC_SET_POWER_CONDITION_TIMER:
        {
            uint32_t timer = M_GETBITRANGE(epcLBA, 23, 8);
            bool units = epcLBA & BIT7;
            bool enable = epcLBA & BIT5;
            bool save = epcLBA & BIT4;
            if (units)
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Power Condition Timer - %s Timer: %" PRIu32 " minutes, Enable: %d, Save: %d", commandName, powerConditionString, timer, enable, save);
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Power Condition Timer - %s Timer: %" PRIu32 " ms, Enable: %d, Save: %d", commandName, powerConditionString, timer * 100, enable, save);
            }
        }
            break;
        case EPC_SET_POWER_CONDITION_STATE:
        {
            bool enable = epcLBA & BIT5;
            bool save = epcLBA & BIT4;
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Power Condition State - %s Enable: %d, Save: %d", commandName, powerConditionString, enable, save);
        }
            break;
        case EPC_ENABLE_EPC_FEATURE_SET:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable EPC Feature", commandName);
            break;
        case EPC_DISABLE_EPC_FEATURE_SET:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable EPC Feature", commandName);
            break;
        case EPC_SET_EPC_POWER_SOURCE:
        {
            uint8_t powerSource = M_GETBITRANGE(subcommandCount, 1, 0);
#define POWER_SOURCE_STRING_LENGTH 21
            char powerSourceString[POWER_SOURCE_STRING_LENGTH] = { 0 };
            switch (powerSource)
            {
            case 1:
                snprintf(powerSourceString, POWER_SOURCE_STRING_LENGTH, "Battery");
                break;
            case 2:
                snprintf(powerSourceString, POWER_SOURCE_STRING_LENGTH, "Not Battery");
                break;
            default:
                snprintf(powerSourceString, POWER_SOURCE_STRING_LENGTH, "Unknown (%01" PRIX8 "h)", powerSource);
                break;
            }
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set EPC Power Source - %s", commandName, powerSourceString);
        }
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown EPC Subcommand (%02" PRIX8 "h) - %s LBA: %07" PRIu32 "h", commandName, subcommand, powerConditionString, epcLBA);
            break;
        }
    }
        break;
    case SF_SET_CACHE_SEGMENTS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Cache Segments - %" PRIu8 " Segments", commandName, subcommandCount);
        break;
    case SF_DISABLE_READ_LOOK_AHEAD_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Read Look-Ahead", commandName);
        break;
    case SF_ENABLE_RELEASE_INTERRUPT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Release Interrupt", commandName);
        break;
    case SF_ENABLE_SERVICE_INTERRUPT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Service Interrupt", commandName);
        break;
    case SF_ENABLE_DISABLE_DATA_TRANSFER_AFTER_ERROR_DETECTION:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Method To Disable Data Transfer After Error Detection", commandName);
        break;
    case SF_LONG_PHYSICAL_SECTOR_ALIGNMENT_ERROR_REPORTING:
        switch (subcommandCount)
        {
        case SF_LPS_DISABLED:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Long Physical Sector Alignment Error Reporting - Disabled", commandName);
            break;
        case SF_LPS_REPORT_ALIGNMENT_ERROR:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Long Physical Sector Alignment Error Reporting - Report Error", commandName);
            break;
        case SF_LPS_REPORT_ALIGNMENT_ERROR_DATA_CONDITION_UNKNOWN:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Long Physical Sector Alignment Error Reporting - Report Error, Data Condition Unknown", commandName);
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Long Physical Sector Alignment Error Reporting - Unknown Mode (%02" PRIX8 "h)", commandName, subcommandCount);
            break;
        }
        break;
    case SF_ENABLE_DISABLE_DSN_FEATURE:
        switch (subcommandCount)
        {
        case SF_DSN_ENABLE:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Device Statistics Notification - Enable", commandName);
            break;
        case SF_DSN_DISABLE:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Device Statistics Notification - Disable", commandName);
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Device Statistics Notification - Unknown Subcommand (%02" PRIX8 "h)", commandName, subcommandCount);
            break;
        }
        break;
    case SF_DISABLE_REVERTING_TO_POWERON_DEFAULTS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Reverting to Poweron Defaults", commandName);
        break;
    case SF_CFA_NOP_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - CFA (%02" PRIX8 "h) - NOP, Accepted for Compatibility", commandName, setFeaturesSubcommand);
        break;
    case SF_DISABLE_ECC:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable ECC", commandName);
        break;
    case SF_DISABLE_8_BIT_DATA_TRANSFERS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable 8-bit Data Transfers", commandName);
        break;
    case SF_DISABLE_VOLITILE_WRITE_CACHE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Volatile Write Cache", commandName);
        break;
    case SF_DISABLE_ALL_AUTOMATIC_DEFECT_REASSIGNMENT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable All Automatic Defect Reassignment", commandName);
        break;
    case SF_DISABLE_APM_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Advanced Power Management", commandName);
        break;
    case SF_DISABLE_PUIS_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Power Up In Standby", commandName);
        break;
    case SF_ENABLE_ECC:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable ECC", commandName);
        break;
    case SF_ADDRESS_OFFSET_RESERVED_BOOT_AREA_METHOD_TECH_REPORT_2:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Address Offser Reserved Boot Area Method %02" PRIX8 "h", commandName, setFeaturesSubcommand);
        break;
    case SF_DISABLE_CFA_POWER_MODE_1:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable CFA Power Mode 1", commandName);
        break;
    case SF_DISABLE_WRITE_READ_VERIFY_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Write-Read-Verify", commandName);
        break;
    case SF_DISABLE_DEVICE_LIFE_CONTROL:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Device Life Control", commandName);
        break;
    case SF_DISABLE_SATA_FEATURE:
        get_SATA_Feature_Control_Command_Info(commandName, false, subcommandCount, lba, commandInfo);
        break;
    case SF_ENABLE_MEDIA_STATUS_NOTIFICATION:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Media Status Notification", commandName);
        break;
    case SF_CFA_NOP_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY_1:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - CFA (%02" PRIX8 "h) - NOP, Accepted for Compatibility", commandName, setFeaturesSubcommand);
        break;
    case SF_CFA_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - CFA (%02" PRIX8 "h) - Accepted for Compatibility", commandName, setFeaturesSubcommand);
        break;
    case SF_ENABLE_RETIRES:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Retries", commandName);
        break;
    case SF_SET_DEVICE_MAXIMUM_AVERAGE_CURRENT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Device Maximum Average Current: %" PRIu16 " mA", commandName, C_CAST(uint16_t, subcommandCount * 4));
        break;
    case SF_ENABLE_READ_LOOK_AHEAD_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Read Look-Ahead", commandName);
        break;
    case SF_SET_MAXIMUM_PREFETCH:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Maximum Prefetch: %" PRIu8 " sectors", commandName, subcommandCount);
        break;
    case SF_LEGACY_SET_4_BYTES_ECC_FOR_READ_WRITE_LONG:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set 4 Bytes ECC Data For Read/Write Long", commandName);
        break;
    case SF_DISABLE_FREE_FALL_CONTROL_FEATURE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Free-Fall Control", commandName);
        break;
    case SF_DISABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Automatic Acoustic Management", commandName);
        break;
    case SF_ENABLE_DISABLE_SENSE_DATA_REPORTING_FEATURE:
        if (subcommandCount == 0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Sense Data Reporting", commandName);
            break;
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Sense Data Reporting", commandName);
            break;
        }
    case SF_ENABLE_DISABLE_SENSE_DATA_RETURN_FOR_SUCCESSFUL_NCQ_COMMANDS:
        if (subcommandCount == 0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Sense Data Reporting For Successful NCQ Commands", commandName);
            break;
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Sense Data Reporting For Successful NCQ Commands", commandName);
            break;
        }
    case SF_ENABLE_REVERTING_TO_POWER_ON_DEFAULTS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Enable Reverting to Poweron Defaults", commandName);
        break;
    case SF_DISABLE_RELEASE_INTERRUPT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Release Interrupt", commandName);
        break;
    case SF_DISABLE_SERVICE_INTERRUPT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Service Interrupt", commandName);
        break;
    case SF_DISABLE_DISABLE_DATA_TRANSFER_AFTER_ERROR_DETECTION:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Disable Method To Disable Data Transfer After Error Detection", commandName);
        break;
    default:
        if ((setFeaturesSubcommand >= 0x56 && setFeaturesSubcommand <= 0x5C)
            || (setFeaturesSubcommand >= 0xD6 && setFeaturesSubcommand <= 0xDC)
            || setFeaturesSubcommand == 0xE0)
        {
            //vendor specific
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Vendor Specific (%" PRIX8 "h), LBA: %07" PRIX32 " Count: %02" PRIX8 "h", commandName, setFeaturesSubcommand, C_CAST(uint32_t, lba), subcommandCount);
        }
        else if ((setFeaturesSubcommand >= 0xF0 /* && setFeaturesSubcommand <= 0xFF */))
        {
            //reserved for CFA
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Reserved for CFA (%" PRIX8 "h), LBA: %07" PRIX32 " Count: %02" PRIX8 "h", commandName, setFeaturesSubcommand, C_CAST(uint32_t, lba), subcommandCount);
        }
        else
        {
            //unknown/reserved
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Feature (%" PRIX8 "h), LBA: %07" PRIX32 " Count: %02" PRIX8 "h", commandName, setFeaturesSubcommand, C_CAST(uint32_t, lba), subcommandCount);
        }
        break;
    }
}

void get_ZAC_Management_In_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t zmAction = M_Nibble0(features);
    uint8_t featuresActionSpecific = M_Byte1(features);
    uint16_t countActionSpecific = count;
    bool featureActionSpecificAvailable = true;
    if (commandOpCode == ATA_RECEIVE_FPDMA)
    {
        countActionSpecific = M_BytesTo2ByteValue(M_Byte1(count), M_Byte1(features));
        featuresActionSpecific = 0;//this is in the AUX register and we cannot read this in this log page.
        featureActionSpecificAvailable = false;
    }
    switch (zmAction)
    {
    case ZM_ACTION_REPORT_ZONES:
    {
        bool partial = featuresActionSpecific & BIT7;
        uint8_t reportingOptions = M_GETBITRANGE(featuresActionSpecific, 5, 0);
        if (featureActionSpecificAvailable)
        {
#define ZONE_REPORT_OPTIONS_STRING_LENGTH 61
            char reportOptionString[ZONE_REPORT_OPTIONS_STRING_LENGTH] = { 0 };
            switch (reportingOptions)
            {
            case ZONE_REPORT_LIST_ALL_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List All Zones");
                break;
            case ZONE_REPORT_LIST_EMPTY_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Empty Zones");
                break;
            case ZONE_REPORT_LIST_IMPLICIT_OPEN_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Implicitly Opened Zones");
                break;
            case ZONE_REPORT_LIST_EXPLICIT_OPEN_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Explicitly Opened Zones");
                break;
            case ZONE_REPORT_LIST_CLOSED_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Closed Zones");
                break;
            case ZONE_REPORT_LIST_FULL_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Full Zones");
                break;
            case ZONE_REPORT_LIST_READ_ONLY_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Read Only Zones");
                break;
            case ZONE_REPORT_LIST_OFFLINE_ZONES:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Offline Zones");
                break;
            case ZONE_REPORT_LIST_ZONES_WITH_RESET_SET_TO_ONE:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Zones with RWP=True");
                break;
            case ZONE_REPORT_LIST_ZONES_WITH_NON_SEQ_SET_TO_ONE:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Zones W/ Non-Sequential Write Resources Active");
                break;
            case ZONE_REPORT_LIST_ALL_ZONES_THAT_ARE_NOT_WRITE_POINTERS:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "List Zones W/ Not Write Pointer Condition");
                break;
            default:
                snprintf(reportOptionString, ZONE_REPORT_OPTIONS_STRING_LENGTH, "Unknown Report Options (%02" PRIX8 "h)", reportingOptions);
                break;
            }
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Report Zones, Zone Locator: %" PRIu64 "  Partial %d  Page Count: %" PRIu16 " Report: %s", commandName, lba, partial, countActionSpecific, reportOptionString);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Report Zones, Zone Locator: %" PRIu64 "  Partial (Unknown)  Page Count: %" PRIu16 " Report: (Unknown)", commandName, lba, countActionSpecific);
        }
    }
    break;
    default:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown ZAC In Command, LBA: %012" PRIX64 " Features: %04" PRIX16 "h Count: %04" PRIX16 "h", commandName, lba, features, count);
        break;
    }
}

void get_ZAC_Management_Out_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, M_ATTR_UNUSED uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t zmAction = M_Nibble0(features);
    uint8_t featuresActionSpecific = M_Byte1(features);
    //uint16_t countActionSpecific = count;
    bool featureActionSpecificAvailable = true;
    if (commandOpCode == ATA_FPDMA_NON_DATA || commandOpCode == ATA_SEND_FPDMA)
    {
//      if (commandOpCode == ATA_FPDMA_NON_DATA)
//      {
//          countActionSpecific = M_BytesTo2ByteValue(M_Byte1(count), M_Byte1(features));
//      }
        featuresActionSpecific = 0;//this is in the AUX register and we cannot read this in this log page.
        featureActionSpecificAvailable = false;
    }
    switch (zmAction)
    {
    case ZM_ACTION_CLOSE_ZONE:
    {
        bool closeAll = featuresActionSpecific & BIT0;
        if (featureActionSpecificAvailable)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Close Zone, Zone ID: %" PRIu64 "  Close All: %d", commandName, lba, closeAll);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Close Zone, Zone ID: %" PRIu64 "  Close All: (Unknown)", commandName, lba);
        }
    }
        break;
    case ZM_ACTION_FINISH_ZONE:
    {
        bool finishAll = featuresActionSpecific & BIT0;
        if (featureActionSpecificAvailable)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Finish Zone, Zone ID: %" PRIu64 "  Finish All: %d", commandName, lba, finishAll);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Finish Zone, Zone ID: %" PRIu64 "  Finish All: (Unknown)", commandName, lba);
        }
    }
        break;
    case ZM_ACTION_OPEN_ZONE:
    {
        bool openAll = featuresActionSpecific & BIT0;
        if (featureActionSpecificAvailable)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Open Zone, Zone ID: %" PRIu64 "  Open All: %d", commandName, lba, openAll);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Open Zone, Zone ID: %" PRIu64 "  Open All: (Unknown)", commandName, lba);
        }
    }
        break;
    case ZM_ACTION_RESET_WRITE_POINTERS:
    {
        bool resetAll = featuresActionSpecific & BIT0;
        if (featureActionSpecificAvailable)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Reset Write Pointers, Zone ID: %" PRIu64 "  Reset All: %d", commandName, lba, resetAll);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Reset Write Pointers, Zone ID: %" PRIu64 "  Reset All: (Unknown)", commandName, lba);
        }
    }
        break;
    default:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown ZAC Out Command, LBA: %012" PRIX64 " Features: %04" PRIX16 "h Count: %04" PRIX16 "h", commandName, lba, features, count);
        break;
    }
}

void get_NCQ_Non_Data_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_Nibble0(features);
    uint8_t tag = M_GETBITRANGE(count, 7, 3);
    uint8_t prio = M_GETBITRANGE(count, 15, 14);//technically subcommand specific...
    switch (subcommand)
    {
    case NCQ_NON_DATA_ABORT_NCQ_QUEUE:
    {
        uint8_t abortType = M_GETBITRANGE(features, 7, 4);
        uint8_t ttag = M_GETBITRANGE(lba, 7, 3);
#define ABORT_TYPE_STRING_LENGTH 31
        char abortTypeString[ABORT_TYPE_STRING_LENGTH] = { 0 };
        switch (abortType)
        {
        case 0:
            snprintf(abortTypeString, ABORT_TYPE_STRING_LENGTH, "Abort All");
            break;
        case 1:
            snprintf(abortTypeString, ABORT_TYPE_STRING_LENGTH, "Abort Streaming");
            break;
        case 2:
            snprintf(abortTypeString, ABORT_TYPE_STRING_LENGTH, "Abort Non-Streaming");
            break;
        case 3:
            snprintf(abortTypeString, ABORT_TYPE_STRING_LENGTH, "Abort Selected. TTAG = %" PRIu8 "", ttag);
            break;
        default:
            snprintf(abortTypeString, ABORT_TYPE_STRING_LENGTH, "Unknown Abort Type (%" PRIX8 "h)", abortType);
            break;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Abort NCQ Queue: %s. Tag: %" PRIu8 " PRIO: %" PRIu8 "", commandName, abortTypeString, tag, prio);
    }
        break;
    case NCQ_NON_DATA_DEADLINE_HANDLING:
    {
        bool rdnc = features & BIT5;
        bool wdnc = features & BIT4;
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Deadline Handling. Tag: %" PRIu8 " WDNC: %d RDNC: %d", commandName, tag, wdnc, rdnc);
    }
        break;
    case NCQ_NON_DATA_HYBRID_DEMOTE_BY_SIZE:
    {
        uint16_t sectorCount = M_BytesTo2ByteValue(M_Byte1(features), M_Byte1(count));
        uint8_t fromPriority = M_GETBITRANGE(features, 7, 4);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Hybrid Demote By Size. Tag: %" PRIu8 " LBA: %" PRIu64 " Count: %" PRIu16 " From Priority: %" PRIu8 "", commandName, tag, lba, sectorCount, fromPriority);
    }
        break;
    case NCQ_NON_DATA_HYBRID_CHANGE_BY_LBA_RANGE:
    {
        uint16_t sectorCount = M_BytesTo2ByteValue(M_Byte1(features), M_Byte1(count));
        bool avoidSpinup = features & BIT4;
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Hybrid Change By LBA Range. Tag: %" PRIu8 " LBA: %" PRIu64 " Count: %" PRIu16 " Avoid Spinup: %d", commandName, tag, lba, sectorCount, avoidSpinup);
    }
        break;
    case NCQ_NON_DATA_HYBRID_CONTROL:
    {
        bool disableCachingMedia = features & BIT7;
        uint8_t dirtyHighThreshold = M_Byte1(lba);
        uint8_t dirtyLowThreshold = M_Byte0(lba);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Hybrid Control. Tag: %" PRIu8 " Disable Caching Media: %d Dirty High Thresh: %" PRIu8 " Dirty Low Thresh: %" PRIu8 "", commandName, tag, disableCachingMedia, dirtyHighThreshold, dirtyLowThreshold);
    }
        break;
    case NCQ_NON_DATA_SET_FEATURES:
    {
#define NCQ_SET_FEATURES_STRING_LENGTH 41
        char ncqSetFeaturesString[NCQ_SET_FEATURES_STRING_LENGTH] = { 0 };
        snprintf(ncqSetFeaturesString, NCQ_SET_FEATURES_STRING_LENGTH, "%s - Set Features. Tag: %" PRIu8 "", commandName, tag);
        get_Set_Features_Command_Info(ncqSetFeaturesString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    case NCQ_NON_DATA_ZERO_EXT:
    {
#define NCQ_ZEROS_EXT_STRING_LENGTH 41
        char ncqZerosExtString[NCQ_ZEROS_EXT_STRING_LENGTH] = { 0 };
        snprintf(ncqZerosExtString, NCQ_ZEROS_EXT_STRING_LENGTH, "%s - Zero Ext. Tag: %" PRIu8 "", commandName, tag);
        get_Zeros_Ext_Command_Info(ncqZerosExtString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    case NCQ_NON_DATA_ZAC_MANAGEMENT_OUT:
    {
#define NCQ_ZAC_MANAGEMENT_OUT_STRING_LENGTH 41
        char ncqZacMgmtOutString[NCQ_ZAC_MANAGEMENT_OUT_STRING_LENGTH] = { 0 };
        snprintf(ncqZacMgmtOutString, NCQ_ZAC_MANAGEMENT_OUT_STRING_LENGTH, "%s - ZAC Management Out. Tag: %" PRIu8 "", commandName, tag);
        get_ZAC_Management_Out_Command_Info(ncqZacMgmtOutString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    default:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Subcommand (%" PRIX8 "h). Tag: %" PRIu8 " Feature: %04" PRIX16 "h Count: %0" PRIX16 "h LBA: %012" PRIX64 "h", commandName, subcommand, tag, features, count, lba);
        break;
    }
}

void get_Receive_FPDMA_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_GETBITRANGE(count, 12, 8);
    uint8_t tag = M_GETBITRANGE(count, 7, 3);
    uint8_t prio = M_GETBITRANGE(count, 15, 14);
    switch (subcommand)
    {
    case RECEIVE_FPDMA_READ_LOG_DMA_EXT:
    {
#define RECEIVE_FPDMA_READ_LOG_STRING_LENGTH 41
        char recieveFPDMAReadLogString[RECEIVE_FPDMA_READ_LOG_STRING_LENGTH] = { 0 };
        snprintf(recieveFPDMAReadLogString, RECEIVE_FPDMA_READ_LOG_STRING_LENGTH, "%s - Read Log Ext DMA. Tag: %" PRIu8 " PRIO: %" PRIu8 "", commandName, tag, prio);
        get_GPL_Log_Command_Info(recieveFPDMAReadLogString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    case RECEIVE_FPDMA_ZAC_MANAGEMENT_IN:
    {
#define NCQ_ZAC_MANAGEMENT_IN_STRING_LENGTH 41
        char ncqZacMgmtInString[NCQ_ZAC_MANAGEMENT_IN_STRING_LENGTH] = { 0 };
        snprintf(ncqZacMgmtInString, NCQ_ZAC_MANAGEMENT_IN_STRING_LENGTH, "%s - ZAC Management In. Tag: %" PRIu8 " PRIO: %" PRIu8 "", commandName, tag, prio);
        get_ZAC_Management_In_Command_Info(ncqZacMgmtInString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    default:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Subcommand (%" PRIX8 "h). Tag: %" PRIu8 " Feature: %04" PRIX16 "h Count: %0" PRIX16 "h LBA: %012" PRIX64 "h", commandName, subcommand, tag, features, count, lba);
        break;
    }
}

void get_Send_FPDMA_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint8_t subcommand = M_GETBITRANGE(count, 12, 8);
    uint8_t tag = M_GETBITRANGE(count, 7, 3);
    uint8_t prio = M_GETBITRANGE(count, 15, 14);
    uint32_t blocksToTransfer = features;
    if (blocksToTransfer == 0)
    {
        blocksToTransfer = 65536;
    }
    switch (subcommand)
    {
    case SEND_FPDMA_DATA_SET_MANAGEMENT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Data Set Management. Tag: %" PRIu8 " PRIO: %" PRIu8 " TRIM: (Unknown) DSM Func: (Unknown) Blocks To Transfer: %" PRIu32 " LBA: %" PRIu64 "", commandName, tag, prio, blocksToTransfer, lba);
        break;
    case SEND_FPDMA_HYBRID_EVICT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Hybrid Evicy. Tag: %" PRIu8 " PRIO: %" PRIu8 " Evict All: (Unknown) Blocks To Transfer: %" PRIu32 "", commandName, tag, prio, blocksToTransfer);
        break;
    case SEND_FPDMA_WRITE_LOG_DMA_EXT:
    {
#define SEND_FPDMA_READ_LOG_STRING_LENGTH 41
        char sendFPDMAReadLogString[SEND_FPDMA_READ_LOG_STRING_LENGTH] = { 0 };
        snprintf(sendFPDMAReadLogString, SEND_FPDMA_READ_LOG_STRING_LENGTH, "%s - Write Log Ext DMA. Tag: %" PRIu8 " PRIO: %" PRIu8 "", commandName, tag, prio);
        get_GPL_Log_Command_Info(sendFPDMAReadLogString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    case SEND_FPDMA_ZAC_MANAGEMENT_OUT:
    {
#define SEND_FPDMA_ZAC_MANAGEMENT_OUT_LENGTH 41
        char ncqZacMgmtOutString[SEND_FPDMA_ZAC_MANAGEMENT_OUT_LENGTH] = { 0 };
        snprintf(ncqZacMgmtOutString, SEND_FPDMA_ZAC_MANAGEMENT_OUT_LENGTH, "%s - ZAC Management Out. Tag: %" PRIu8 " PRIO: %" PRIu8 "", commandName, tag, prio);
        get_ZAC_Management_Out_Command_Info(ncqZacMgmtOutString, commandOpCode, features, count, lba, device, commandInfo);
    }
        break;
    case SEND_FPDMA_DATA_SET_MANAGEMENT_XL:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Data Set Management XL. Tag: %" PRIu8 " PRIO: %" PRIu8 " TRIM: (Unknown) DSM Func: (Unknown) Blocks To Transfer: %" PRIu32 " LBA: %" PRIu64 "", commandName, tag, prio, blocksToTransfer, lba);
        break;
    default:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown Subcommand (%" PRIX8 "h). Tag: %" PRIu8 " Feature: %04" PRIX16 "h Count: %0" PRIX16 "h LBA: %012" PRIX64 "h", commandName, subcommand, tag, features, count, lba);
        break;
    }
}

void get_Command_Info(uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    //TODO: some commands leave some registers reserved. Add handling when some of these reserved registers are set to non-zero values
    switch (commandOpCode)
    {
    case ATA_NOP_CMD:
        switch (features)
        {
        case 0:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "NOP");
            break;
        case 1:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "NOP (Auto Poll)");
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "NOP (subcommand %02" PRIx8 "h", C_CAST(uint8_t, features));
            break;
        }
        break;
    case ATA_DATA_SET_MANAGEMENT_CMD:
        if (features & BIT0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management - TRIM");
        }
        else
        {
            uint8_t dsmFunction = M_Byte1(features);
            switch (dsmFunction)
            {
            case 0x00://reserved
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management - Reserved DSM function");
                break;
            case 0x01://markup LBA ranges
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management - Markup LBA ranges");
                break;
            default://unknown or not defined as of ACS4
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management - Unknown DSM function - %" PRIu8 "", dsmFunction);
                break;
            }
        }
        break;
    case ATA_DATA_SET_MANAGEMENT_XL_CMD:
        if (features & BIT0)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management XL - TRIM");
        }
        else
        {
            uint8_t dsmFunction = M_Byte1(features);
            switch (dsmFunction)
            {
            case 0x00://reserved
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management XL - Reserved DSM function");
                break;
            case 0x01://markup LBA ranges
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management XL - Markup LBA ranges");
                break;
            default://unknown or not defined as of ACS4
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Data Set Management XL - Unknown DSM function - %" PRIu8 "", dsmFunction);
                break;
            }
        }
        break;
    case ATA_DEV_RESET:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Device Reset");
        break;
    case ATA_REQUEST_SENSE_DATA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Request Sense Data");
        break;
    case ATA_RECALIBRATE://this can have various values for the lower nibble which conflict with new command standards
    case 0x11:
    //case ATA_GET_PHYSICAL_ELEMENT_STATUS://or recalibrate? check the count register...it should be non-zero
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
        if (commandOpCode == ATA_GET_PHYSICAL_ELEMENT_STATUS && count != 0)
        {
            uint8_t filter = M_GETBITRANGE(features, 15, 14);
            uint8_t reportType = M_GETBITRANGE(features, 11, 8);
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Get Physical Element Status. Starting element: %" PRIu64 " Filter: %" PRIu8" Report Type: %" PRIu8 "", lba, filter, reportType);
        }
        else if (count != 0)
        {
            //TODO: features, lba, etc
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Unknown Command (%02" PRIX8 "h)", commandOpCode);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Recalibrate (%02" PRIX8 "h)", commandOpCode);
        }
        break;
    case ATA_READ_SECT:
        get_Read_Write_Command_Info("Read Sectors", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_SECT_NORETRY:
        get_Read_Write_Command_Info("Read Sectors (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_LONG_RETRY:
        get_Read_Write_Command_Info("Read Long", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_LONG_NORETRY:
        get_Read_Write_Command_Info("Read Long (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_SECT_EXT:
        get_Read_Write_Command_Info("Read Sectors Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_DMA_EXT:
        get_Read_Write_Command_Info("Read DMA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_DMA_QUE_EXT:
        get_Read_Write_Command_Info("Read DMA Queued Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_MAX_ADDRESS_EXT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Read Max Address Ext");//no other worthwhile inputs to this command to report...every other register is N/A
        break;
    case ATA_READ_READ_MULTIPLE_EXT:
        get_Read_Write_Command_Info("Read Multiple Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_STREAM_DMA_EXT:
        get_Read_Write_Command_Info("Read Stream DMA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_STREAM_EXT:
        get_Read_Write_Command_Info("Read Stream Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_LOG_EXT:
        get_GPL_Log_Command_Info("Read Log Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_SECT:
        get_Read_Write_Command_Info("Write Sectors", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_SECT_NORETRY:
        get_Read_Write_Command_Info("Write Sectors (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_LONG_RETRY:
        get_Read_Write_Command_Info("Write Long", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_LONG_NORETRY:
        get_Read_Write_Command_Info("Write Long (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_SECT_EXT:
        get_Read_Write_Command_Info("Write Sectors Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_EXT:
        get_Read_Write_Command_Info("Write DMA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_QUE_EXT:
        get_Read_Write_Command_Info("Write DMA Queued Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SET_MAX_EXT:
        get_Set_Max_Address_Command_Info("Set Max Address Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_MULTIPLE_EXT:
        get_Read_Write_Command_Info("Write Multiple Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_STREAM_DMA_EXT:
        get_Read_Write_Command_Info("Write Stream DMA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_STREAM_EXT:
        get_Read_Write_Command_Info("Write Stream Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_SECTV_RETRY:
        get_Read_Write_Command_Info("Write Verify", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_FUA_EXT:
        get_Read_Write_Command_Info("Write DMA FUA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_QUE_FUA_EXT:
        get_Read_Write_Command_Info("Write DMA Queued FUA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_LOG_EXT_CMD:
        get_GPL_Log_Command_Info("Write Log Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_VERIFY_RETRY:
        get_Read_Write_Command_Info("Read Verify", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_VERIFY_NORETRY:
        get_Read_Write_Command_Info("Read Verify (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_VERIFY_EXT:
        get_Read_Write_Command_Info("Read Verify Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_ZEROS_EXT:
        get_Zeros_Ext_Command_Info("Zeros Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_UNCORRECTABLE_EXT:
    {
        uint8_t uncorrectableOption = M_Byte0(features);
        uint32_t numberOfSectors = count;
#define UNCORRECTABLE_OPTION_STRING_LENGTH 31
        char uncorrectableOptionString[UNCORRECTABLE_OPTION_STRING_LENGTH] = { 0 };
        if (numberOfSectors == 0)
        {
            numberOfSectors = 65536;
        }
        switch (uncorrectableOption)
        {
        case WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING://psuedo
            snprintf(uncorrectableOptionString, UNCORRECTABLE_OPTION_STRING_LENGTH, "Psuedo with logging");
            break;
        case WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING://flagged
            snprintf(uncorrectableOptionString, UNCORRECTABLE_OPTION_STRING_LENGTH, "Flagged without logging");
            break;
        case WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_5AH://vendor specific
        case WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_A5H://vendor specific
            snprintf(uncorrectableOptionString, UNCORRECTABLE_OPTION_STRING_LENGTH, "Vendor Specific (%02" PRIX8 "h)", uncorrectableOption);
            break;
        default://reserved/unknown
            snprintf(uncorrectableOptionString, UNCORRECTABLE_OPTION_STRING_LENGTH, "Unknown Mode (%02" PRIX8 "h)", uncorrectableOption);
            break;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Uncorrectable Ext - %s  LBA: %" PRIu64 "  Count: %" PRIu32 "", uncorrectableOptionString, lba, numberOfSectors);
    }
        break;
    case ATA_READ_LOG_EXT_DMA:
        get_GPL_Log_Command_Info("Read Log Ext DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_ZONE_MANAGEMENT_IN:
        get_ZAC_Management_In_Command_Info("ZAC Management In", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_FORMAT_TRACK:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Format Tracks");
        break;
    case ATA_CONFIGURE_STREAM:
    {
        uint8_t defaultCCTL = M_Byte1(features);
        bool addRemoveStream = features & BIT7;
        bool readWriteStream = features & BIT6;
        uint8_t streamID = M_GETBITRANGE(features, 2, 0);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Configure Stream, Default CCTL: %" PRIu8 ", Add/Remove Stream: %d, readWriteStream: %d, Stream ID: %" PRIu8 "", defaultCCTL, addRemoveStream, readWriteStream, streamID);
    }
        break;
    case ATA_WRITE_LOG_EXT_DMA:
        get_GPL_Log_Command_Info("Write Log Ext DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_TRUSTED_NON_DATA:
        get_Trusted_Command_Info("Trusted Non-Data", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_TRUSTED_RECEIVE:
        get_Trusted_Command_Info("Trusted Receive", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_TRUSTED_RECEIVE_DMA:
        get_Trusted_Command_Info("Trusted Receive DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_TRUSTED_SEND:
        get_Trusted_Command_Info("Trusted Send", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_TRUSTED_SEND_DMA:
        get_Trusted_Command_Info("Trusted Send DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_FPDMA_QUEUED_CMD:
        get_Read_Write_Command_Info("Read FPDMA Queued", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_FPDMA_QUEUED_CMD:
        get_Read_Write_Command_Info("Write FPDMA Queued", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_FPDMA_NON_DATA:
        get_NCQ_Non_Data_Command_Info("NCQ Non-Data", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SEND_FPDMA:
        get_Send_FPDMA_Command_Info("Send FPDMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_RECEIVE_FPDMA:
        get_Receive_FPDMA_Command_Info("Receive FPDMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SEEK_CMD://TODO: seek can be 7xh....but that also conflicts with new command definitions
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77://ATA_SET_DATE_AND_TIME_EXT
    case 0x78://ATA_ACCESSABLE_MAX_ADDR
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C://ATA_REMOVE_AND_TRUNCATE
    case 0x7D:
    case 0x7E:
    case 0x7F:
    {
        uint32_t seekLBA = (lba & MAX_28_BIT_LBA) | (M_Nibble0(device) << 24);
        uint16_t seekCylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
        uint8_t seekHead = M_Nibble0(device);
        uint8_t seekSector = M_Byte0(lba);
        bool isLBAMode = device & LBA_MODE_BIT;
        if (commandOpCode == ATA_ACCESSABLE_MAX_ADDR)
        {
            //if feature is nonzero, definitely a AMAC command
            if ((features == 0 && lba == 0 && device & LBA_MODE_BIT /*check if seeking to LBA 0 since this could be missed in second check*/) || (features == 0 && lba > 0 /*this check is for legacy CHS seek AND Any other LBA to seek to*/))
            {
                //legacy seek command
                if (isLBAMode)
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - LBA: %" PRIu32 "", commandOpCode, seekLBA);

                }
                else
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 "", commandOpCode, seekCylinder, seekHead, seekSector);
                }
            }
            else
            {
                //AMAC command
                get_AMAC_Command_Info("Accessible Max Address", commandOpCode, features, count, lba, device, commandInfo);
            }
        }
        else if (commandOpCode == ATA_SET_DATE_AND_TIME_EXT)
        {
            //this one will be difficult to tell the difference between...LBA should be less that 24bits, LBAmode should not be on, and head field should be zero
            if (M_Nibble0(device) > 0 || ((device & LBA_MODE_BIT) && lba < MAX_28_BIT_LBA))
            {
                //most likely a seek
                if (isLBAMode)
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - LBA: %" PRIu32 "", commandOpCode, seekLBA);

                }
                else
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 "", commandOpCode, seekCylinder, seekHead, seekSector);
                }
            }
            else //timestamp is # milliseconds since Jan 1 1970...so we should reliably get here since we shouldn't ever have an LBA value less than 24bits...
            {
                //set data and time (TODO: convert this to a current date and time)
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Date And Tme Ext - Timestamp - %" PRIu64 " milliseconds", lba);
            }
        }
        else if (commandOpCode == ATA_REMOVE_AND_TRUNCATE)
        {
            //features and count will be used for remove and truncate, but not the legacy seek command :)
            if (features || count || lba > MAX_28_BIT_LBA || !(device & LBA_MODE_BIT))//LBA may be zero. device register should NOT have the lba bit set for this command
            {
                //remove and truncate command
                uint32_t elementIdentifier = M_WordsTo4ByteValue(features, count);
                if (lba > 0)
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Remove And Truncate - Element ID: %" PRIX32 "h - Requested Max LBA: %" PRIu64 "", elementIdentifier, lba);
                }
                else
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Remove And Truncate - Element ID: %" PRIX32 "h", elementIdentifier);
                }
            }
            else
            {
                //legacy seek
                if (isLBAMode)
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - LBA: %" PRIu32 "", commandOpCode, seekLBA);

                }
                else
                {
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 "", commandOpCode, seekCylinder, seekHead, seekSector);
                }
            }
        }
        else
        {
            if (isLBAMode)
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - LBA: %" PRIu32 "", commandOpCode, seekLBA);

            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek (%02" PRIX8 "h) - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 "", commandOpCode, seekCylinder, seekHead, seekSector);
            }
        }
    }
        break;
    case ATA_EXEC_DRV_DIAG:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Execute Drive Diagnostic");
        break;
    case ATA_INIT_DRV_PARAM:
    {
        uint8_t sectorsPerTrack = M_Byte0(count);
        uint8_t maxHead = M_Nibble0(device);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Initialize Drive Parameters. Logical Sectors Per Track: %" PRIu8 "  Max Head: %" PRIu8 "", sectorsPerTrack, maxHead);
    }
        break;
    case ATA_DOWNLOAD_MICROCODE:
        get_Download_Command_Info("Download Microcode", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_DOWNLOAD_MICROCODE_DMA:
        get_Download_Command_Info("Download Microcode DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_LEGACY_ALT_STANDBY_IMMEDIATE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Alternate Standby Immediate (94h)");
        break;
    case ATA_LEGACY_ALT_IDLE_IMMEDIATE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Alternate Idle Immediate (95h)");
        break;
    case ATA_LEGACY_ALT_STANDBY:
        get_Idle_Or_Standby_Command_Info("Alternate Standby (96h)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_LEGACY_ALT_IDLE:
        get_Idle_Or_Standby_Command_Info("Alternate Standby (97h)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_LEGACY_ALT_CHECK_POWER_MODE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Alternate Check Power Mode (98h)");
        break;
    case ATA_LEGACY_ALT_SLEEP:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Alternate Sleep (99h)");
        break;
    case ATA_ZONE_MANAGEMENT_OUT:
        get_ZAC_Management_Out_Command_Info("ZAC Management Out", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATAPI_COMMAND:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "ATA Packet Command");
        break;
    case ATAPI_IDENTIFY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Identify Packet Device");
        break;
    case ATA_SMART:
        get_SMART_Command_Info("SMART", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_DCO:
        get_DCO_Command_Info("DCO", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SET_SECTOR_CONFIG_EXT:
    {
        uint8_t descriptorIndex = M_GETBITRANGE(count, 2, 0);
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Sector Configuration Ext - Descriptor: %" PRIu8 ", Command Check: %" PRIX16 "h", descriptorIndex, features);
    }
        break;
    case ATA_SANITIZE:
        get_Sanitize_Command_Info("Sanitize", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_NV_CACHE:
        get_NV_Cache_Command_Info("NV Cache", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_MULTIPLE:
        get_Read_Write_Command_Info("Read Multiple", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_MULTIPLE:
        get_Read_Write_Command_Info("Write Multiple", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SET_MULTIPLE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Multiple - DRQ Data Block Count: %" PRIu8 "", M_Byte0(count));
        break;
    case ATA_READ_DMA_QUEUED_CMD:
        get_Read_Write_Command_Info("Read DMA Queued", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_DMA_RETRY:
        get_Read_Write_Command_Info("Read DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_DMA_NORETRY:
        get_Read_Write_Command_Info("Read DMA (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_RETRY:
        get_Read_Write_Command_Info("Write DMA", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_NORETRY:
        get_Read_Write_Command_Info("Write DMA (No Retry)", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_DMA_QUEUED_CMD:
        get_Read_Write_Command_Info("Write DMA Queued", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_WRITE_MULTIPLE_FUA_EXT:
        get_Read_Write_Command_Info("Write Multiple FUA Ext", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_GET_MEDIA_STATUS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Get Media Status");
        break;
    case ATA_ACK_MEDIA_CHANGE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Acknowledge Media Change");
        break;
    case ATA_POST_BOOT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Post Boot");
        break;
    case ATA_PRE_BOOT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Pre Boot");
        break;
    case ATA_DOOR_LOCK:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Door Lock");
        break;
    case ATA_DOOR_UNLOCK:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Door Unlock");
        break;
    case ATA_STANDBY_IMMD:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Standby Immediate");
        break;
    case ATA_IDLE_IMMEDIATE_CMD:
        if (M_Byte0(features) == IDLE_IMMEDIATE_UNLOAD_FEATURE)
        {
            uint32_t idleImmdLBA = C_CAST(uint32_t, lba & 0x00FFFFFFFF) | (M_Nibble0(device) << 24);
            if (IDLE_IMMEDIATE_UNLOAD_LBA == idleImmdLBA)
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Idle Immediate - Unload");
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Idle Immediate - Unload. Invalid LBA Signature: %07" PRIu32 "", idleImmdLBA);
            }
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Idle Immediate");
        }
        break;
    case ATA_STANDBY:
        get_Idle_Or_Standby_Command_Info("Standby", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_IDLE:
        get_Idle_Or_Standby_Command_Info("Idle", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_READ_BUF:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Read Buffer");
        break;
    case ATA_CHECK_POWER_MODE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Check Power Mode");
        break;
    case ATA_SLEEP_CMD:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Sleep");
        break;
    case ATA_FLUSH_CACHE:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Flush Cache");
        break;
    case ATA_WRITE_BUF:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Buffer");
        break;
    case ATA_READ_BUF_DMA:
        //case ATA_LEGACY_WRITE_SAME:
        if (M_Byte0(features) == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS || M_Byte0(features) == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)
        {
            if (M_Byte0(features) == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS)
            {
                if (device & LBA_MODE_BIT)
                {
                    uint32_t writeSameLBA = M_Nibble0(device) << 24;
                    writeSameLBA |= M_DoubleWord0(lba) & UINT32_C(0x00FFFFFF);//grabbing first 24 bits only since the others should be zero
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Same - LBA: %" PRIu32 " Count: %" PRIu8 "", writeSameLBA, M_Byte0(count));
                }
                else
                {
                    uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                    uint8_t head = M_Nibble0(device);
                    uint8_t sector = M_Byte0(lba);
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Same - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu8 "", cylinder, head, sector, M_Byte0(count));
                }
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Same - All Sectors");
            }
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Read Buffer DMA");
        }
        break;
    case ATA_FLUSH_CACHE_EXT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Flush Cache Ext");
        break;
    case ATA_WRITE_BUF_DMA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Buffer DMA");
        break;
    case ATA_IDENTIFY:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Identify");
        break;
    case ATA_MEDIA_EJECT:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Media Eject");
        break;
    case ATA_IDENTIFY_DMA:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Identify DMA");
        break;
    case ATA_SET_FEATURE:
        get_Set_Features_Command_Info("Set Features", commandOpCode, features, count, lba, device, commandInfo);
        break;
    case ATA_SECURITY_SET_PASS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Set Password");
        break;
    case ATA_SECURITY_UNLOCK_CMD:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Unlock");
        break;
    case ATA_SECURITY_ERASE_PREP:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Erase Prepare");
        break;
    case ATA_SECURITY_ERASE_UNIT_CMD:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Erase Unit");
        break;
    case ATA_SECURITY_FREEZE_LOCK_CMD:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Freeze Lock");
        break;
    case ATA_SECURITY_DISABLE_PASS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Security Disable Password");
        break;
    case ATA_READ_MAX_ADDRESS:
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Read Max Address");
        break;
    case ATA_SET_MAX:
        get_Set_Max_Address_Command_Info("Set Max Address", commandOpCode, features, count, lba, device, commandInfo);
        break;
    default:
        if ((commandOpCode >= 0x80 && commandOpCode <= 0x8F)
            || commandOpCode == 0x9A
            || (commandOpCode >= 0xC0 && commandOpCode <= 0xC3)
            || commandOpCode == 0xF0
            || commandOpCode == 0xF7
            || (commandOpCode >= 0xFA /* && commandOpCode <= 0xFF */)
            )
        {
            //NOTE: The above if is far from perfect...there are some commands that were once VU in old standards that have been defined in newer ones...this is as close as I care to get this.
            //NOTE2: A couple of the op codes above may be for CFA, or reserved for CFA. Don't care right now since we are unlikely to see a CFA device with this code.
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Vendor Unique Command %02" PRIx8 "h", commandOpCode);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Unknown Command %02" PRIx8 "h", commandOpCode);
        }
        break;
    }
}

#define TIMESTRING_MAX_LEN 30
void convert_Milliseconds_To_Time_String(uint64_t milliseconds, char timeString[TIMESTRING_MAX_LEN + 1])
{
    uint8_t days = C_CAST(uint8_t, milliseconds / (UINT64_C(24) * UINT64_C(60) * UINT64_C(60) * UINT64_C(1000)));
    milliseconds %= (UINT64_C(24) * UINT64_C(60) * UINT64_C(60) * UINT64_C(1000));
    uint8_t hours = C_CAST(uint8_t, milliseconds / (UINT64_C(60) * UINT64_C(60) * UINT64_C(1000)));
    milliseconds %= (UINT64_C(60) * UINT64_C(60) * UINT64_C(1000));
    uint8_t minutes = C_CAST(uint8_t, milliseconds / (UINT64_C(60) * UINT64_C(1000)));
    milliseconds %= (UINT64_C(60) * UINT64_C(1000));
    uint8_t seconds = C_CAST(uint8_t, milliseconds / UINT64_C(1000));
    milliseconds %= UINT64_C(1000);
    snprintf(timeString, TIMESTRING_MAX_LEN, "%" PRIu8 "D:%" PRIu8 "H:%" PRIu8 "M:%" PRIu8 "S:%" PRIu64 "MS", days, hours, minutes, seconds, milliseconds);
}

bool is_Read_Write_Command(uint8_t commandOpCode)
{
    bool isReadWrite = false;
    switch (commandOpCode)
    {
    case ATA_WRITE_LONG_NORETRY:
    case ATA_READ_LONG_NORETRY:
    case ATA_READ_LONG_RETRY:
    case ATA_WRITE_LONG_RETRY:
    case ATA_READ_SECT_NORETRY:
    case ATA_WRITE_SECT_NORETRY:
    case ATA_READ_DMA_NORETRY:
    case ATA_WRITE_DMA_NORETRY:
    case ATA_READ_SECT:
    case ATA_WRITE_SECT:
    case ATA_WRITE_SECTV_RETRY:
    case ATA_READ_MULTIPLE:
    case ATA_WRITE_MULTIPLE:
    case ATA_READ_DMA_RETRY:
    case ATA_WRITE_DMA_RETRY:
    case ATA_READ_SECT_EXT:
    case ATA_READ_DMA_EXT:
    case ATA_READ_READ_MULTIPLE_EXT:
    case ATA_WRITE_MULTIPLE_FUA_EXT:
    case ATA_WRITE_SECT_EXT:
    case ATA_WRITE_DMA_EXT:
    case ATA_WRITE_MULTIPLE_EXT:
    case ATA_WRITE_DMA_FUA_EXT:
    case ATA_WRITE_STREAM_DMA_EXT:
    case ATA_WRITE_STREAM_EXT:
    case ATA_READ_STREAM_DMA_EXT:
    case ATA_READ_STREAM_EXT:
    case ATA_READ_VERIFY_NORETRY:
    case ATA_READ_VERIFY_RETRY:
    case ATA_READ_VERIFY_EXT:
    case ATA_READ_FPDMA_QUEUED_CMD:
    case ATA_WRITE_FPDMA_QUEUED_CMD:
    case ATA_READ_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUE_FUA_EXT:
    case ATA_WRITE_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUEUED_CMD:
    case ATA_READ_DMA_QUEUED_CMD:
        isReadWrite = true;
        break;
    default://unknown command...
        break;
    }
    return isReadWrite;
}

bool is_Ext_Read_Write_Command(uint8_t commandOpCode)
{
    bool isReadWrite = false;
    switch (commandOpCode)
    {
    case ATA_READ_SECT_EXT:
    case ATA_READ_DMA_EXT:
    case ATA_READ_READ_MULTIPLE_EXT:
    case ATA_WRITE_MULTIPLE_FUA_EXT:
    case ATA_WRITE_SECT_EXT:
    case ATA_WRITE_DMA_EXT:
    case ATA_WRITE_MULTIPLE_EXT:
    case ATA_WRITE_DMA_FUA_EXT:
    case ATA_WRITE_STREAM_DMA_EXT:
    case ATA_WRITE_STREAM_EXT:
    case ATA_READ_STREAM_DMA_EXT:
    case ATA_READ_STREAM_EXT:
    case ATA_READ_VERIFY_EXT:
    case ATA_READ_FPDMA_QUEUED_CMD:
    case ATA_WRITE_FPDMA_QUEUED_CMD:
    case ATA_READ_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUE_FUA_EXT:
    case ATA_WRITE_DMA_QUE_EXT:
        isReadWrite = true;
        break;
    default://unknown command...
        break;
    }
    return isReadWrite;
}

bool is_Stream_Command(uint8_t commandOpCode)
{
    bool isStream = false;
    switch (commandOpCode)
    {
    case ATA_WRITE_STREAM_DMA_EXT:
    case ATA_WRITE_STREAM_EXT:
    case ATA_READ_STREAM_DMA_EXT:
    case ATA_READ_STREAM_EXT:
        isStream = true;
        break;
    default://unknown command...
        break;
    }
    return isStream;
}

bool is_DMA_Queued_Command(uint8_t commandOpCode)
{
    bool isDMAQueued = false;
    switch (commandOpCode)
    {
    case ATA_READ_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUE_FUA_EXT:
    case ATA_WRITE_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUEUED_CMD:
    case ATA_READ_DMA_QUEUED_CMD:
        isDMAQueued = true;
        break;
    default://unknown command...
        break;
    }
    return isDMAQueued;
}

bool is_Possible_Recalibrate_Command(uint8_t commandOpCodeThatCausedError)
{
    if (M_Nibble1(commandOpCodeThatCausedError) == 0x1)//All possible recalibrate commands start with nibble 0 set to 1
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool is_Recalibrate_Command(uint8_t commandOpCodeThatCausedError)
{
    if (commandOpCodeThatCausedError == ATA_RECALIBRATE)
    {
        return true;
    }
    else
    {
        return false;
    }
}

#define ATA_ERROR_INFO_MAX_LENGTH UINT8_C(4096) //making this bigger than we need for the moment
#define ATA_ERROR_MESSAGE_MAX_LENGTH UINT8_C(256) //making this bigger than we need for the moment
#define ATA_STATUS_MESSAGE_MAX_LENGTH UINT8_C(256) //making this bigger than we need for the moment
void get_Error_Info(uint8_t commandOpCodeThatCausedError, uint8_t commandDeviceReg, uint8_t status, uint8_t error, M_ATTR_UNUSED uint16_t count, uint64_t lba, uint8_t device, M_ATTR_UNUSED uint8_t transportSpecific, char errorInfo[ATA_ERROR_INFO_MAX_LENGTH + 1])
{
    //bool isDMAQueued = is_DMA_Queued_Command(commandOpCodeThatCausedError);
    bool isStream = is_Stream_Command(commandOpCodeThatCausedError);
    bool isReadWrite = is_Read_Write_Command(commandOpCodeThatCausedError);
    bool isRecal = is_Recalibrate_Command(commandOpCodeThatCausedError);//NOTE: This will only catch case 0x10. The function is_Possible_Recalibrate_Command can also be used, but some of those op-codes HAVE been repurposed so it is less accurate!

    char statusMessage[ATA_STATUS_MESSAGE_MAX_LENGTH] = { 0 };
    char errorMessage[ATA_ERROR_MESSAGE_MAX_LENGTH] = { 0 };

    //TODO: Start with Status bits!
    if (status & ATA_STATUS_BIT_DEVICE_FAULT)
    {
        //device fault occurred as a result of the command!
        snprintf(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Device Fault");
    }
    if (status & ATA_STATUS_BIT_ALIGNMENT_ERROR)
    {
        //device reports an alignment error
        if (strlen(statusMessage) > 0)
        {
            common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, ", ");
        }
        common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Alignment Error");
    }
    if (isStream && (status & ATA_STATUS_BIT_DEFERRED_WRITE_ERROR))
    {
        if (strlen(statusMessage) > 0)
        {
            common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, ", ");
        }
        //streaming deferred write error
        common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Deferred Write Error");
    }

    if (status & ATA_STATUS_BIT_ERROR)
    {
        if (strlen(statusMessage) > 0)
        {
            common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, ", ");
        }
        common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Error Reg Valid");

        //TODO: Parse error field bits
        if (error & ATA_ERROR_BIT_ABORT)
        {
            common_String_Concat(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Abort");
        }
        if (error & ATA_ERROR_BIT_INTERFACE_CRC)//abort bit will also be set to 1 if this is set to 1
        {
            if (strlen(errorMessage) > 0)
            {
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
            }
            common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "Interface CRC Error");
        }
        if (error & ATA_ERROR_BIT_UNCORRECTABLE_DATA)
        {
            if (strlen(errorMessage) > 0)
            {
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
            }
            common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "Uncorrectable Data");
        }
        if (error & ATA_ERROR_BIT_ID_NOT_FOUND)// - media access and possibly commands to set max lba
        {
            if (strlen(errorMessage) > 0)
            {
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
            }
            common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "ID Not Found");
        }
        if (isRecal && (error & ATA_ERROR_BIT_TRACK_ZERO_NOT_FOUND))// - recalibrate commands only
        {
            if (strlen(errorMessage) > 0)
            {
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
            }
            common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "Track Zero Not Found");
        }
        if (isStream && (error & ATA_ERROR_BIT_COMMAND_COMPLETION_TIME_OUT))// - streaming
        {
            if (strlen(errorMessage) > 0)
            {
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
            }
            common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "Command Completion Time Out");
        }
        if(strlen(errorMessage) == 0)
        {
            if (is_Possible_Recalibrate_Command(commandOpCodeThatCausedError))
            {
                if (strlen(errorMessage) > 0)
                {
                    common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
                }
                common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "(Likely) Track Zero Not Found");
            }
            else
            {
                if (strlen(errorMessage) > 0)
                {
                    common_String_Concat(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, ", ");
                }
                //unknown error, possibly recalibrate command + track zero not found....
                char *dup = strdup(errorMessage);
                if(dup)
                {
                    snprintf(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "%sUnknown Error Condition (%02" PRIX8 "h)", dup, error);
                    safe_Free(dup)
                }
            }
        }
    }
    else
    {
        if (strlen(statusMessage) == 0)
        {
            snprintf(statusMessage, ATA_STATUS_MESSAGE_MAX_LENGTH, "Unknown Status Bits Set: %02" PRIX8 "h)", status);
        }
        snprintf(errorMessage, ATA_ERROR_MESSAGE_MAX_LENGTH, "No Error Bits Set");
    }
    if (isReadWrite)
    {
        if (commandDeviceReg & LBA_MODE_BIT)
        {
            if (is_Ext_Read_Write_Command(commandOpCodeThatCausedError))
            {
                snprintf(errorInfo, ATA_ERROR_INFO_MAX_LENGTH, "Status: %s\tError: %s\tLBA: %" PRIu64 "\tDevice: %02" PRIX8 "", statusMessage, errorMessage, lba, device);
            }
            else
            {
                uint32_t smallLba = (lba & MAX_28_BIT_LBA) | (M_Nibble0(device) << 24);
                snprintf(errorInfo, ATA_ERROR_INFO_MAX_LENGTH, "Status: %s\tError: %s\tLBA: %" PRIu32 "\tDevice: %02" PRIX8 "", statusMessage, errorMessage, smallLba, device);
            }
        }
        else //CHS read/write command
        {
            //not handling ext...shouldn't be an issue since CHS and 48bit don't really go together.
            snprintf(errorInfo, ATA_ERROR_INFO_MAX_LENGTH, "Status: %s\tError: %s\tCyl: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 "\tDevice: %02" PRIX8 "", statusMessage, errorMessage, M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba)), M_Nibble0(device), M_Byte0(lba), device);
        }
    }
    else
    {
        snprintf(errorInfo, ATA_ERROR_INFO_MAX_LENGTH, "Status: %s\tError: %s\tLBA: %012" PRIX64 "h\tDevice: %02" PRIX8 "", statusMessage, errorMessage, lba, device);
    }
}

void print_ATA_Comprehensive_SMART_Error_Log(ptrComprehensiveSMARTErrorLog errorLogData, bool genericOutput)
{
    if (errorLogData)
    {
        printf("SMART Comprehensive Error Log");
        if (errorLogData->extLog)
        {
            printf(" (EXT)");
        }
        printf("- Version %" PRIu8 ":\n", errorLogData->version);
        if (errorLogData->numberOfEntries == 0)
        {
            printf("\tNo errors found!\n");
        }
        else
        {
            printf("\tFound %" PRIu8" errors! Total Error Count: %" PRIu16 "\n", errorLogData->numberOfEntries, errorLogData->deviceErrorCount);
            if (!errorLogData->checksumsValid)
            {
                printf("\tWARNING: Invalid checksum was detected when reading SMART Error log data!\n");
            }

            if (genericOutput)
            {
                //print out a key for commands and errors!
                //Generic printout of SMART error log:
                //Command & Error need to know when it's a entry from the ext log or not so they can print it correctly!
                //Command:
                // CD=Command  FT=Feature  LL=LbaLo  LM=LBAMid  LH=LBAHi  DH=Device/Head  SC=SectorCount  DC=DeviceControl
                // CD FT SC LL LM LH DH DC
                //Ext Command:
                // CD=Command  FT=Feature  FTe=FeatureExt  LL=LbaLo  LM=LBAMid  LH=LBAHi  LLe=LbaLoExt  LMe=LBAMidExt  LHe=LBAHiExt  DH=Device/Head  SC=SectorCount SCe=SectorCountExt  DC=DeviceControl
                // CD FT FTe SC SCe LL LM LH LLe LMe LHe DH DC
                //Error:
                // ST=Status  ER=Error  LL=LbaLo  LM=LBAMid  LH=LBAHi  DH=Device/Head  SC=SectorCount  DC=DeviceControl
                // ST ER SC LL LM LH DH DC
                //Ext Error:
                // ST=Status  ER=Error  LL=LbaLo  LM=LBAMid  LH=LBAHi  LLe=LbaLoExt  LMe=LBAMidExt  LHe=LBAHiExt  DH=Device/Head  SC=SectorCount SCe=SectorCountExt  DC=DeviceControl
                // ST ER SC SCe LL LM LH LLe LMe LHe DH DC
                if (errorLogData->extLog)
                {
                    printf("\t-----Command Key-----\n");
                    printf("\tCD - Command     \tFT - Feature     \tFTe - Feature Ext\n");
                    printf("\tSC - Sector Count\tSCe - Sector Count Ext\n");
                    printf("\tLL - LBA Low     \tLM - LBA Mid     \tLH - LBA Hi\n");
                    printf("\tLLe - LBA Low Ext\tLMe - LBA Mid Ext\tLHe - LBA Hi Ext\n");
                    printf("\tDH - Device/Head \tDC - Device Control (transport specific)\n");
                    printf("\t------Error Key------\n");
                    printf("\tST - Status      \tER - Error\n");
                    printf("\tSC - Sector Count\tSCe - Sector Count Ext\n");
                    printf("\tLL - LBA Low     \tLM - LBA Mid     \tLH - LBA Hi\n");
                    printf("\tLLe - LBA Low Ext\tLMe - LBA Mid Ext\tLHe - LBA Hi Ext\n");
                    printf("\tDH - Device/Head \tDC - Device Control\tVU Bytes - Extended Error Info (Vendor Unique)\n");
                    printf("\t---------------------\n");
                }
                else
                {
                    printf("\t-----Command Key-----\n");
                    printf("\tCD - Command     \tFT - Feature\n");
                    printf("\tSC - Sector Count\tLL - LBA Low\n");
                    printf("\tLM - LBA Mid     \tLH - LBA Hi\n");
                    printf("\tDH - Device/Head \tDC - Device Control (transport specific)\n");
                    printf("\t------Error Key------\n");
                    printf("\tST - Status      \tER - Error\n");
                    printf("\tSC - Sector Count\tLL - LBA Low\n");
                    printf("\tLM - LBA Mid     \tLH - LBA Hi\n");
                    printf("\tDH - Device/Head \tVU Bytes - Extended Error Info (Vendor Unique)\n");
                    printf("\t---------------------\n");
                }
            }

            uint16_t totalErrorCountLimit = SMART_COMPREHENSIVE_ERRORS_MAX;
            if (errorLogData->extLog)
            {
                totalErrorCountLimit = SMART_EXT_COMPREHENSIVE_ERRORS_MAX;
            }
            for (uint8_t iter = 0; iter < errorLogData->numberOfEntries && iter < totalErrorCountLimit; ++iter)
            {
                printf("\n===============================================\n");
                printf("Error %" PRIu16 " - Drive State: ", iter + 1);
                uint8_t errorState = errorLogData->smartError[iter].error.state;
                if (errorLogData->extLog)
                {
                    errorState = errorLogData->extSmartError[iter].extError.state;
                }
                switch (M_Nibble0(errorState))
                {
                case 0:
                    printf("Unknown");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 1:
                    printf("Sleep");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 2:
                    printf("Standby");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 3:
                    printf("Active/Idle");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 4:
                    printf("Executing Off-line or self test");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                default:
                    if (M_Nibble0(errorState) >= 5 && M_Nibble0(errorState) <= 0x0A)
                    {
                        printf("Reserved (%02" PRIX8 "h)", errorState);
                    }
                    else
                    {
                        printf("Vendor Specific (%02" PRIX8 "h)", errorState);
                    }
                    break;
                }
                printf(" Life Timestamp: ");
                uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
                uint64_t lifeTimeStampSeconds = 0;
                if (errorLogData->extLog)
                {
                    lifeTimeStampSeconds = errorLogData->extSmartError[iter].extError.lifeTimestamp * 3600;
                }
                else
                {
                    lifeTimeStampSeconds = errorLogData->smartError[iter].error.lifeTimestamp * 3600;
                }
                convert_Seconds_To_Displayable_Time(lifeTimeStampSeconds, &years, &days, &hours, &minutes, &seconds);
                print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
                printf("\n");
                uint8_t numberOfCommandsBeforeError = errorLogData->smartError[iter].numberOfCommands;
                if (errorLogData->extLog)
                {
                    numberOfCommandsBeforeError = errorLogData->extSmartError[iter].numberOfCommands;
                }
                //Putting these vars here because we may need to look at them while parsing the error reason.
                uint16_t features = 0, count = 0;
                uint8_t commandOpCode = 0, device = 0;
                uint64_t lba = 0;
                //Loop through and print out commands leading up to the error
                //call get command info function above
                if (genericOutput)
                {
                    if (errorLogData->extLog)
                    {
                        //printf the command register format before printing commands
                        printf("CD FT FTe SC SCe LL LM LH LLe LMe LHe DH DC\tTimeStamp\n");
                    }
                    else
                    {
                        //printf the command register format before printing commands
                        printf("CD FT SC LL LM LH DH DC\tTimeStamp\n");
                    }
                }
                for (uint8_t commandIter = UINT8_C(5) - numberOfCommandsBeforeError; commandIter < UINT8_C(5); ++commandIter)
                {
                    uint32_t timestampMilliseconds = 0;
                    char timestampString[TIMESTRING_MAX_LEN + 1] = { 0 };
                    bool isHardReset = false;
                    bool isSoftReset = false;
                    if (errorLogData->extLog)
                    {
                        features = M_BytesTo2ByteValue(errorLogData->extSmartError[iter].extCommand[commandIter].featureExt, errorLogData->extSmartError[iter].extCommand[commandIter].feature);
                        count = M_BytesTo2ByteValue(errorLogData->extSmartError[iter].extCommand[commandIter].countExt, errorLogData->extSmartError[iter].extCommand[commandIter].count);
                        commandOpCode = errorLogData->extSmartError[iter].extCommand[commandIter].contentWritten;
                        device = errorLogData->extSmartError[iter].extCommand[commandIter].device;
                        lba = M_BytesTo8ByteValue(0, 0, errorLogData->extSmartError[iter].extCommand[commandIter].lbaHiExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaMidExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaLowExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaHi, errorLogData->extSmartError[iter].extCommand[commandIter].lbaMid, errorLogData->extSmartError[iter].extCommand[commandIter].lbaLow);
                        timestampMilliseconds = errorLogData->extSmartError[iter].extCommand[commandIter].timestampMilliseconds;
                        isSoftReset = errorLogData->extSmartError[iter].extCommand[commandIter].deviceControl & DEVICE_CONTROL_SOFT_RESET;
                        if (errorLogData->extSmartError[iter].extCommand[commandIter].deviceControl == UINT8_MAX)
                        {
                            isHardReset = true;
                        }
                    }
                    else
                    {
                        features = errorLogData->smartError[iter].command[commandIter].feature;
                        count = errorLogData->smartError[iter].command[commandIter].count;
                        commandOpCode = errorLogData->smartError[iter].command[commandIter].contentWritten;
                        device = errorLogData->smartError[iter].command[commandIter].device;
                        lba = M_BytesTo4ByteValue(0, errorLogData->smartError[iter].command[commandIter].lbaHi, errorLogData->smartError[iter].command[commandIter].lbaMid, errorLogData->smartError[iter].command[commandIter].lbaLow);
                        isSoftReset = errorLogData->smartError[iter].command[commandIter].transportSpecific & DEVICE_CONTROL_SOFT_RESET;
                        timestampMilliseconds = errorLogData->smartError[iter].command[commandIter].timestampMilliseconds;
                        if (errorLogData->smartError[iter].command[commandIter].transportSpecific == UINT8_MAX)
                        {
                            isHardReset = true;
                        }
                    }
                    //convert the timestamp to something simple.
                    convert_Milliseconds_To_Time_String(timestampMilliseconds, timestampString);
                    if (genericOutput)
                    {
                        if (errorLogData->extLog)
                        {
                            //printf("CD FT FTe SC SCe LL LM LH LLe LMe LHe DH DC\tTimeStamp\n");
                            printf("%02" PRIX8 " %02" PRIX8 " %02" PRIX8 "  %02" PRIX8 " %02" PRIX8 "  %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "  %02" PRIX8 "  %02" PRIX8 "  %02" PRIX8 " %02" PRIX8 "\t%s\n", errorLogData->extSmartError[iter].extCommand[commandIter].contentWritten, errorLogData->extSmartError[iter].extCommand[commandIter].feature, errorLogData->extSmartError[iter].extCommand[commandIter].featureExt, errorLogData->extSmartError[iter].extCommand[commandIter].count, errorLogData->extSmartError[iter].extCommand[commandIter].countExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaLow, errorLogData->extSmartError[iter].extCommand[commandIter].lbaMid, errorLogData->extSmartError[iter].extCommand[commandIter].lbaHi, errorLogData->extSmartError[iter].extCommand[commandIter].lbaLowExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaMidExt, errorLogData->extSmartError[iter].extCommand[commandIter].lbaHiExt, errorLogData->extSmartError[iter].extCommand[commandIter].device, errorLogData->extSmartError[iter].extCommand[commandIter].deviceControl, timestampString);
                        }
                        else
                        {
                            //printf("CD FT SC LL LM LH DH DC\tTimeStamp\n");
                            printf("%02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "\t%s\n", errorLogData->smartError[iter].command[commandIter].contentWritten, errorLogData->smartError[iter].command[commandIter].feature, errorLogData->smartError[iter].command[commandIter].count, errorLogData->smartError[iter].command[commandIter].lbaLow, errorLogData->smartError[iter].command[commandIter].lbaMid, errorLogData->smartError[iter].command[commandIter].lbaHi, errorLogData->smartError[iter].command[commandIter].device, errorLogData->smartError[iter].command[commandIter].transportSpecific, timestampString);
                        }
                    }
                    else
                    {
                        if (isHardReset)
                        {
                            printf("%" PRIu8 " - %s - Hardware Reset\n", commandIter + 1, timestampString);
                        }
                        else if (isSoftReset)
                        {
                            printf("%" PRIu8 " - %s - Software Reset\n", commandIter + 1, timestampString);
                        }
                        else
                        {
                            //translate into a command
                            char commandDescription[ATA_COMMAND_INFO_MAX_LENGTH] = { 0 };
                            get_Command_Info(commandOpCode, features, count, lba, device, commandDescription);
                            printf("%" PRIu8 " - %s - %s\n", commandIter + 1, timestampString, commandDescription);
                        }
                    }
                }
                //TODO: print out the error command! highlight in red? OR some other thing like a -> to make it clear what command was the error?
                uint8_t status = 0, error = 0, errorDevice = 0, errorDeviceControl = 0;
                uint64_t errorlba = 0;
                uint16_t errorCount = 0;
                if (errorLogData->extLog)
                {
                    //ext
                    status = errorLogData->extSmartError[iter].extError.status;
                    error = errorLogData->extSmartError[iter].extError.error;
                    errorDevice = errorLogData->extSmartError[iter].extError.device;
                    errorCount = M_BytesTo2ByteValue(errorLogData->extSmartError[iter].extError.countExt, errorLogData->extSmartError[iter].extError.count);
                    errorlba = M_BytesTo8ByteValue(0, 0, errorLogData->extSmartError[iter].extError.lbaHiExt, errorLogData->extSmartError[iter].extError.lbaMidExt, errorLogData->extSmartError[iter].extError.lbaLowExt, errorLogData->extSmartError[iter].extError.lbaHi, errorLogData->extSmartError[iter].extError.lbaMid, errorLogData->extSmartError[iter].extError.lbaLow);
                    errorDeviceControl = errorLogData->extSmartError[iter].extError.transportSpecific;
                }
                else
                {
                    //non-ext
                    status = errorLogData->smartError[iter].error.status;
                    error = errorLogData->smartError[iter].error.error;
                    errorDevice = errorLogData->smartError[iter].error.device;
                    errorCount = errorLogData->smartError[iter].error.count;
                    errorlba = M_BytesTo4ByteValue(0, errorLogData->smartError[iter].error.lbaHi, errorLogData->smartError[iter].error.lbaMid, errorLogData->smartError[iter].error.lbaLow);
                    //errorDeviceControl is not available here.
                }
                if (genericOutput)
                {
                    if (errorLogData->extLog)
                    {
                        //first print out the format
                        //printf the error register format before printing commands
                        printf("\nST ER     SC SCe LL LM LH LLe LMe LHe DH DC\tVU Bytes\n");
                        printf("%02" PRIX8 " %02" PRIX8 "     %02" PRIX8 " %02" PRIX8 "  %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "  %02" PRIX8 "  %02" PRIX8 "  %02" PRIX8 " %02" PRIX8 "\t", errorLogData->extSmartError[iter].extError.status, errorLogData->extSmartError[iter].extError.error, errorLogData->extSmartError[iter].extError.count, errorLogData->extSmartError[iter].extError.countExt, errorLogData->extSmartError[iter].extError.lbaLow, errorLogData->extSmartError[iter].extError.lbaMid, errorLogData->extSmartError[iter].extError.lbaHi, errorLogData->extSmartError[iter].extError.lbaLowExt, errorLogData->extSmartError[iter].extError.lbaMidExt, errorLogData->extSmartError[iter].extError.lbaHiExt, errorLogData->extSmartError[iter].extError.device, errorLogData->extSmartError[iter].extError.transportSpecific);
                        for (uint8_t vuIter = 0; vuIter < 19; ++vuIter)
                        {
                            printf("%02" PRIX8 "", errorLogData->extSmartError[iter].extError.extendedErrorInformation[vuIter]);
                        }
                        printf("\n");
                    }
                    else
                    {
                        //first print out the format
                        //printf the error register format before printing commands
                        printf("\nST ER SC LL LM LH DH\tVU Bytes\n");
                        printf("%02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "\t", errorLogData->smartError[iter].error.status, errorLogData->smartError[iter].error.error, errorLogData->smartError[iter].error.count, errorLogData->smartError[iter].error.lbaLow, errorLogData->smartError[iter].error.lbaMid, errorLogData->smartError[iter].error.lbaHi, errorLogData->smartError[iter].error.device);
                        for (uint8_t vuIter = 0; vuIter < 19; ++vuIter)
                        {
                            printf("%02" PRIX8 "", errorLogData->smartError[iter].error.extendedErrorInformation[vuIter]);
                        }
                        printf("\n");
                    }
                }
                else
                {
                    char errorString[ATA_ERROR_INFO_MAX_LENGTH + 1] = { 0 };
                    get_Error_Info(commandOpCode, device, status, error, errorCount, errorlba, errorDevice, errorDeviceControl, errorString);
                    printf("Error: %s\n", errorString);
                }
            }
        }
    }
}

//Ext commands reported in the summary log will be truncated to 28bits! Data will not be as accurate!
void print_ATA_Summary_SMART_Error_Log(ptrSummarySMARTErrorLog errorLogData, bool genericOutput)
{
    if (errorLogData)
    {
        printf("SMART Summary Error Log");
        printf("- Version %" PRIu8 ":\n", errorLogData->version);
        if (errorLogData->numberOfEntries == 0)
        {
            printf("\tNo errors found!\n");
        }
        else
        {
            printf("\tFound %" PRIu8" errors! Total Error Count: %" PRIu16 "\n", errorLogData->numberOfEntries, errorLogData->deviceErrorCount);
            if (!errorLogData->checksumsValid)
            {
                printf("\tWARNING: Invalid checksum was detected when reading SMART Error log data!\n");
            }

            if (genericOutput)
            {
                //print out a key for commands and errors!
                //Generic printout of SMART error log:
                //Command & Error need to know when it's a entry from the ext log or not so they can print it correctly!
                //Command:
                // CD=Command  FT=Feature  LL=LbaLo  LM=LBAMid  LH=LBAHi  DH=Device/Head  SC=SectorCount  DC=DeviceControl
                // CD FT SC LL LM LH DH DC
                //Ext Command:
                // CD=Command  FT=Feature  FTe=FeatureExt  LL=LbaLo  LM=LBAMid  LH=LBAHi  LLe=LbaLoExt  LMe=LBAMidExt  LHe=LBAHiExt  DH=Device/Head  SC=SectorCount SCe=SectorCountExt  DC=DeviceControl
                // CD FT FTe SC SCe LL LM LH LLe LMe LHe DH DC
                //Error:
                // ST=Status  ER=Error  LL=LbaLo  LM=LBAMid  LH=LBAHi  DH=Device/Head  SC=SectorCount  DC=DeviceControl
                // ST ER SC LL LM LH DH DC
                //Ext Error:
                // ST=Status  ER=Error  LL=LbaLo  LM=LBAMid  LH=LBAHi  LLe=LbaLoExt  LMe=LBAMidExt  LHe=LBAHiExt  DH=Device/Head  SC=SectorCount SCe=SectorCountExt  DC=DeviceControl
                // ST ER SC SCe LL LM LH LLe LMe LHe DH DC
                printf("\t-----Command Key-----\n");
                printf("\tCD - Command     \tFT - Feature\n");
                printf("\tSC - Sector Count\tLL - LBA Low\n");
                printf("\tLM - LBA Mid     \tLH - LBA Hi\n");
                printf("\tDH - Device/Head \tDC - Device Control (transport specific)\n");
                printf("\t------Error Key------\n");
                printf("\tST - Status      \tER - Error\n");
                printf("\tSC - Sector Count\tLL - LBA Low\n");
                printf("\tLM - LBA Mid     \tLH - LBA Hi\n");
                printf("\tDH - Device/Head \tVU Bytes - Extended Error Info (Vendor Unique)\n");
                printf("\t---------------------\n");
            }

            uint16_t totalErrorCountLimit = SMART_SUMMARY_ERRORS_MAX;
            for (uint8_t iter = 0; iter < errorLogData->numberOfEntries && iter < totalErrorCountLimit; ++iter)
            {
                printf("\n===============================================\n");
                printf("Error %" PRIu16 " - Drive State: ", iter + 1);
                uint8_t errorState = errorLogData->smartError[iter].error.state;
                switch (M_Nibble0(errorState))
                {
                case 0:
                    printf("Unknown");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 1:
                    printf("Sleep");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 2:
                    printf("Standby");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 3:
                    printf("Active/Idle");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                case 4:
                    printf("Executing Off-line or self test");
                    if (genericOutput)
                    {
                        printf("(%02" PRIX8 "h)", errorState);
                    }
                    break;
                default:
                    if (M_Nibble0(errorState) >= 5 && M_Nibble0(errorState) <= 0x0A)
                    {
                        printf("Reserved (%02" PRIX8 "h)", errorState);
                    }
                    else
                    {
                        printf("Vendor Specific (%02" PRIX8 "h)", errorState);
                    }
                    break;
                }
                printf(" Life Timestamp: ");
                uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(errorLogData->smartError[iter].error.lifeTimestamp * 3600, &years, &days, &hours, &minutes, &seconds);
                print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
                printf("\n");
                uint8_t numberOfCommandsBeforeError = errorLogData->smartError[iter].numberOfCommands;
                //Putting these vars here because we may need to look at them while parsing the error reason.
                uint16_t features = 0, count = 0;
                uint8_t commandOpCode = 0, device = 0;
                uint64_t lba = 0;
                //Loop through and print out commands leading up to the error
                //call get command info function above
                if (genericOutput)
                {
                    //printf the command register format before printing commands
                    printf("CD FT SC LL LM LH DH DC\tTimeStamp\n");
                }
                for (uint8_t commandIter = UINT8_C(5) - numberOfCommandsBeforeError; commandIter < UINT8_C(5); ++commandIter)
                {
                    uint32_t timestampMilliseconds = 0;
                    char timestampString[TIMESTRING_MAX_LEN + 1] = { 0 };
                    bool isHardReset = false;
                    bool isSoftReset = false;
                    features = errorLogData->smartError[iter].command[commandIter].feature;
                    count = errorLogData->smartError[iter].command[commandIter].count;
                    commandOpCode = errorLogData->smartError[iter].command[commandIter].contentWritten;
                    device = errorLogData->smartError[iter].command[commandIter].device;
                    lba = M_BytesTo4ByteValue(0, errorLogData->smartError[iter].command[commandIter].lbaHi, errorLogData->smartError[iter].command[commandIter].lbaMid, errorLogData->smartError[iter].command[commandIter].lbaLow);
                    isSoftReset = errorLogData->smartError[iter].command[commandIter].transportSpecific & DEVICE_CONTROL_SOFT_RESET;
                    timestampMilliseconds = errorLogData->smartError[iter].command[commandIter].timestampMilliseconds;
                    if (errorLogData->smartError[iter].command[commandIter].transportSpecific == UINT8_MAX)
                    {
                        isHardReset = true;
                    }
                    //convert the timestamp to something simple.
                    convert_Milliseconds_To_Time_String(timestampMilliseconds, timestampString);
                    if (genericOutput)
                    {
                        //printf("CD FT SC LL LM LH DH DC\tTimeStamp\n");
                        printf("%02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "\t%s\n", errorLogData->smartError[iter].command[commandIter].contentWritten, errorLogData->smartError[iter].command[commandIter].feature, errorLogData->smartError[iter].command[commandIter].count, errorLogData->smartError[iter].command[commandIter].lbaLow, errorLogData->smartError[iter].command[commandIter].lbaMid, errorLogData->smartError[iter].command[commandIter].lbaHi, errorLogData->smartError[iter].command[commandIter].device, errorLogData->smartError[iter].command[commandIter].transportSpecific, timestampString);
                    }
                    else
                    {
                        if (isHardReset)
                        {
                            printf("%" PRIu8 " - %s - Hardware Reset\n", commandIter + 1, timestampString);
                        }
                        else if (isSoftReset)
                        {
                            printf("%" PRIu8 " - %s - Software Reset\n", commandIter + 1, timestampString);
                        }
                        else
                        {
                            //translate into a command
                            char commandDescription[ATA_COMMAND_INFO_MAX_LENGTH] = { 0 };
                            get_Command_Info(commandOpCode, features, count, lba, device, commandDescription);
                            printf("%" PRIu8 " - %s - %s\n", commandIter + 1, timestampString, commandDescription);
                        }
                    }
                }
                //TODO: print out the error command! highlight in red? OR some other thing like a -> to make it clear what command was the error?
                uint8_t status = 0, error = 0, errorDevice = 0, errorDeviceControl = 0;
                uint64_t errorlba = 0;
                uint16_t errorCount = 0;
                status = errorLogData->smartError[iter].error.status;
                error = errorLogData->smartError[iter].error.error;
                errorDevice = errorLogData->smartError[iter].error.device;
                errorCount = errorLogData->smartError[iter].error.count;
                errorlba = M_BytesTo4ByteValue(0, errorLogData->smartError[iter].error.lbaHi, errorLogData->smartError[iter].error.lbaMid, errorLogData->smartError[iter].error.lbaLow);
                //errorDeviceControl is not available here.
                if (genericOutput)
                {
                    //first print out the format
                    //printf the error register format before printing commands
                    printf("\nST ER SC LL LM LH DH\tVU Bytes\n");
                    printf("%02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 " %02" PRIX8 "\t", errorLogData->smartError[iter].error.status, errorLogData->smartError[iter].error.error, errorLogData->smartError[iter].error.count, errorLogData->smartError[iter].error.lbaLow, errorLogData->smartError[iter].error.lbaMid, errorLogData->smartError[iter].error.lbaHi, errorLogData->smartError[iter].error.device);
                    for (uint8_t vuIter = 0; vuIter < 19; ++vuIter)
                    {
                        printf("%02" PRIX8 "", errorLogData->smartError[iter].error.extendedErrorInformation[vuIter]);
                    }
                    printf("\n");
                }
                else
                {
                    char errorString[ATA_ERROR_INFO_MAX_LENGTH + 1] = { 0 };
                    get_Error_Info(commandOpCode, device, status, error, errorCount, errorlba, errorDevice, errorDeviceControl, errorString);
                    printf("Error: %s\n", errorString);
                }
            }
        }
    }
}
