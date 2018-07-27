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
// \file smart.c
// \brief This file defines the functions related to SMART features on a drive (attributes, Status check)

#include "operations_Common.h"
#include "smart.h"
#include "usb_hacks.h"
#include "logs.h"

int get_SMART_Attributes(tDevice *device, smartLogData * smartAttrs)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE && is_SMART_Enabled(device))
    {
        ataSMARTAttribute *currentAttribute = NULL;
        uint16_t            smartIter = 0;
        uint8_t *ATAdataBuffer = (uint8_t *)calloc(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t));
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
                currentAttribute = (ataSMARTAttribute *)&ATAdataBuffer[smartIter];
                if (currentAttribute->attributeNumber > 0 && currentAttribute->attributeNumber < 255)
                {
                    smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute->attributeNumber].valid = true;
                    memcpy(&smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute->attributeNumber].data, currentAttribute, sizeof(ataSMARTAttribute));
                    //check if it's warrantied (This should work on Seagate drives at least)
                    if (currentAttribute->status & BIT0)
                    {
                        smartAttrs->attributes.ataSMARTAttr.attributes[currentAttribute->attributeNumber].isWarrantied = true;
                    }
                }
            }
            memset(ATAdataBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ata_SMART_Read_Thresholds(device, ATAdataBuffer, LEGACY_DRIVE_SEC_SIZE))
            {
                ataSMARTThreshold *currentThreshold = NULL;
                for (smartIter = ATA_SMART_BEGIN_ATTRIBUTES; smartIter < ATA_SMART_END_ATTRIBUTES; smartIter += ATA_SMART_ATTRIBUTE_SIZE)
                {
                    currentThreshold = (ataSMARTThreshold*)&ATAdataBuffer[smartIter];
                    if (currentThreshold->attributeNumber > 0 && currentThreshold->attributeNumber < 255)
                    {
                        smartAttrs->attributes.ataSMARTAttr.attributes[currentThreshold->attributeNumber].thresholdDataValid = true;
                        memcpy(&smartAttrs->attributes.ataSMARTAttr.attributes[currentThreshold->attributeNumber].thresholdData, currentThreshold, sizeof(ataSMARTThreshold));
                    }
                }
            }
        }
        free(ATAdataBuffer);
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
        if (VERBOSITY_QUIET < g_verbosity)
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
            sprintf(*attributeName, "Read Error Rate");
            break;
        case 3://spin up time
            sprintf(*attributeName, "Spin Up Time");
            break;
        case 4://start stop count
            sprintf(*attributeName, "Start/Stop Count");
            break;
        case 5://retired sectors count
            sprintf(*attributeName, "Retired Sectors Count");
            break;
        case 7://Seek Error Rate
            sprintf(*attributeName, "Seek Error Rate");
            break;
        case 9: //Power on Hours
            sprintf(*attributeName, "Power On Hours");
            break;
        case 10: //Spin Retry Count
            sprintf(*attributeName, "Spin Retry Count");
            break;
        case 12: //Drive Power Cycle Count
            sprintf(*attributeName, "Drive Power Cycle Count");
            break;
        case 174: //Unexpected Power Loss Count
            sprintf(*attributeName, "Unexpected Power Loss Count");
            break;
        case 183://PHY Counter Events
            sprintf(*attributeName, "PHY Counter Events");
            break;
        case 184://IOEDC Count
            sprintf(*attributeName, "IOEDC Count");
            break;
        case 187: //Reported Un-correctable
            sprintf(*attributeName, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            sprintf(*attributeName, "Command Timeout");
            break;
        case 189: //High Fly Writes
            sprintf(*attributeName, "High Fly Writes");
            break;
        case 190: //Airflow Temperature
            sprintf(*attributeName, "Airflow Temperature");
            break;
        case 191: //Shock Sensor Counter
            sprintf(*attributeName, "Shock Sensor Counter");
            break;
        case 192: //Emergency Retract Count
            sprintf(*attributeName, "Emergency Retract Count");
            break;
        case 193: //Load-Unload Count
            sprintf(*attributeName, "Load-Unload Count");
            break;
        case 194: //Temperature
            sprintf(*attributeName, "Temperature");
            break;
        case 195: //ECC On the Fly Count
            sprintf(*attributeName, "ECC On The Fly Count");
            break;
        case 197: //Pending-Sparing Count
            sprintf(*attributeName, "Pending-Sparing Count");
            break;
        case 199: //Ultra DMA CRC Error
            sprintf(*attributeName, "Ultra DMA CRC Error");
            break;
        case 200: //Pressure Measurement Limit
            sprintf(*attributeName, "Pressure Measurement Limit");
            break;
        case 230: //Life Curve Status
            sprintf(*attributeName, "Life Curve Status");
            break;
        case 231: //SSD Life Left
            sprintf(*attributeName, "SSD Life Left");
            break;
        case 235: //SSD Power Loss Mgmt Life Left
            sprintf(*attributeName, "SSD Power Less Mgmt Life Left");
            break;
        case 240: //Head flight Hours
            sprintf(*attributeName, "Head Flight Hours");
            break;
        case 241: //Lifetime Writes from Host
            sprintf(*attributeName, "Lifetime Writes From Host");
            break;
        case 242: //Lifetime Reads from Host
            sprintf(*attributeName, "Lifetime Reads From Host");
            break;
        case 254: //Free Fall Event
            sprintf(*attributeName, "Free Fall Event");
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
            sprintf(*attributeName, "Read Error Rate");
            break;
        case 5://retired sectors count
            sprintf(*attributeName, "Retired Sectors Count");
            break;
        case 9: //Power on Hours
            sprintf(*attributeName, "Power On Hours");
            break;
        case 12: //Drive Power Cycle Count
            sprintf(*attributeName, "Drive Power Cycle Count");
            break;
        case 171: //Program Fail Count
            sprintf(*attributeName, "Program Fail Count");
            break;
        case 172: //Erase Fail Count
            sprintf(*attributeName, "Erase Fail Count");
            break;
        case 181: //Program Fail Count
            sprintf(*attributeName, "Program Fail Count");
            break;
        case 182: //Erase Fail Count
            sprintf(*attributeName, "Erase Fail Count");
            break;
        case 194: //Temperature
            sprintf(*attributeName, "Temperature");
            break;
        case 201: //Soft Error Rate
            sprintf(*attributeName, "Soft Error Rate");
            break;
        case 204: //Soft ECC Correction Rate
            sprintf(*attributeName, "Soft ECC Correction Rate");
            break;
        case 231: //SSD Life Left
            sprintf(*attributeName, "SSD Life Left");
            break;
        case 234: //Lifetime Write to Flash
            sprintf(*attributeName, "Lifetime Writes To Flash in GiB");
            break;
        case 241: //Lifetime Writes from Host
            sprintf(*attributeName, "Lifetime Writes From Host in GiB");
            break;
        case 242: //Lifetime Reads from Host
            sprintf(*attributeName, "Lifetime Reads From Host in GiB");
            break;
        case 250: //Lifetime NAND Read Retries
            sprintf(*attributeName, "Lifetime NAND Read Retries");
            break;
        default:
            break;
        }
        break;
    case SAMSUNG:
        switch (attributeNumber)
        {
        case 1://read error rate
            sprintf(*attributeName, "Read Error Rate");
            break;
        case 2: //Throughput Performance
            sprintf(*attributeName, "Throughput Performance");
            break;
        case 3://spin up time
            sprintf(*attributeName, "Spin Up Time");
            break;
        case 4://start stop count
            sprintf(*attributeName, "Start/Stop Count");
            break;
        case 5://retired sectors count
            sprintf(*attributeName, "Retired Sectors Count");
            break;
        case 7://Seek Error Rate
            sprintf(*attributeName, "Seek Error Rate");
            break;
        case 8://seek time performance.
            sprintf(*attributeName, "Seek Time Performance");
            break;
        case 9: //Power on Hours
            sprintf(*attributeName, "Power On Hours");
            break;
        case 10: //Spin Retry Count
            sprintf(*attributeName, "Spin Retry Count");
            break;
        case 11: //calibration retry count
            sprintf(*attributeName, "Calibration Retry Count");
            break;
        case 12: //Drive Power Cycle Count
            sprintf(*attributeName, "Drive Power Cycle Count");
            break;
        case 184://End to End detection
            sprintf(*attributeName, "End To End Detection");
            break;
        case 187: //Reported Un-correctable
            sprintf(*attributeName, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            sprintf(*attributeName, "Command Timeout");
            break;
        case 190: //Airflow Temperature
            sprintf(*attributeName, "Airflow Temperature");
            break;
        case 191: //Shock Sensor Counter
            sprintf(*attributeName, "Shock Sensor Counter");
            break;
        case 192: //Emergency Retract Count
            sprintf(*attributeName, "Emergency Retract Count");
            break;
        case 193: //Load-Unload Count
            sprintf(*attributeName, "Load-Unload Count");
            break;
        case 194: //Temperature
            sprintf(*attributeName, "Temperature");
            break;
        case 195: //ECC On the Fly Count
            sprintf(*attributeName, "ECC On The Fly Count");
            break;
        case 196: //Re-allocate Sector Event
            sprintf(*attributeName, "Re-allocate Sector Event");
            break;
        case 197: //Pending-Sparing Count
            sprintf(*attributeName, "Pending Sector Count");
            break;
        case 198://offlince uncorrectable sectors
            sprintf(*attributeName, "Offline Uncorrectable Sectors");
            break;
        case 199: //Ultra DMA CRC Error
            sprintf(*attributeName, "Ultra DMA CRC Error");
            break;
        case 200: //Write Error Rate
            sprintf(*attributeName, "Write Error Rate");
            break;
        case 201: //Soft Error Rate
            sprintf(*attributeName, "Soft Error Rate");
            break;
        case 223: //Load Retry Count
            sprintf(*attributeName, "Load Retry Count");
            break;
        case 225: //Load Cycle Count
            sprintf(*attributeName, "Load Cycle Count");
            break;
        default:
            break;
        }
        break;
    case MAXTOR: //names are from here: https://www.smartmontools.org/wiki/AttributesMaxtor
        switch (attributeNumber)
        {
        case 1: //raw read error rate
            sprintf(*attributeName, "Raw Read Error Rate");
            break;
        case 2: //throughput performance
            sprintf(*attributeName, "Throughput Performance");
            break;
        case 3: //spin-up time
            sprintf(*attributeName, "Spin Up Time");
            break;
        case 4: //start/stop count
            sprintf(*attributeName, "Start/Stop Count");
            break;
        case 5: //Reallocated Sector Count
            sprintf(*attributeName, "Reallocated Sector Count");
            break;
        case 6: //start/stop count
            sprintf(*attributeName, "Start/Stop Count");
            break;
        case 7: //seek error Rate
            sprintf(*attributeName, "Seek Error Rate");
            break;
        case 8: //seek time performance
            sprintf(*attributeName, "Seek Time Performance");
            break;
        case 9: //power on hours
            sprintf(*attributeName, "Power-On Hours");
            break;
        case 10: //spin-up retry count
            sprintf(*attributeName, "Spin-Up Retry Count");
            break;
        case 11: //calibration retry count
            sprintf(*attributeName, "Calibration Retry Count");
            break;
        case 12: //power cycle count
            sprintf(*attributeName, "Power Cycle Count");
            break;
        case 13: //soft read error rate
            sprintf(*attributeName, "Soft Read Error Rate");
            break;
        case 192: //power-off retract cycle count
            sprintf(*attributeName, "Power-Off Retract Cycle Count");
            break;
        case 193: //Load/Unload Cycle Count
            sprintf(*attributeName, "Load/Unload Cycle Count");
            break;
        case 194: //HDA Temperature
            sprintf(*attributeName, "HDA Temperature");
            break;
        case 195: //Hardware ECC Recovered
            sprintf(*attributeName, "Hardware ECC Recovered");
            break;
        case 196: //Reallocated Event Count
            sprintf(*attributeName, "Re-allocate Event Count");
            break;
        case 197: //Current Pending Sector Count
            sprintf(*attributeName, "Current Pending Sector Count");
            break;
        case 198: //Offline Scan Uncorrectable Count
            sprintf(*attributeName, "Off-line Uncorrectable Count");
            break;
        case 199: //UltraDMA CRC Error Rate
            sprintf(*attributeName, "Ultra DMA CRC Error Rate");
            break;
        case 200: //Write Error Rate
            sprintf(*attributeName, "Write Error Rate");
            break;
        case 201: //Soft Read Error Rate
            sprintf(*attributeName, "Soft Read Error Rate");
            break;
        case 202: //Data Addres Mark Errors
            sprintf(*attributeName, "Data Address Mark Errors");
            break;
        case 203: //run out cancel
            sprintf(*attributeName, "Run Out Cancel");
            break;
        case 204: //Soft ECC Correction
            sprintf(*attributeName, "Soft ECC Correction");
            break;
        case 205: //Thermal Asperity Rate
            sprintf(*attributeName, "Thermal Asperity Rate");
            break;
        case 206: //Flying Height
            sprintf(*attributeName, "Flying Height");
            break;
        case 207: //Spin High Current
            sprintf(*attributeName, "Spin High Current");
            break;
        case 208: //Spin Buzz
            sprintf(*attributeName, "Spin Buzz");
            break;
        case 209: //Offline Seek Performance
            sprintf(*attributeName, "Offline Seek Performance");
            break;
        case 220: //Disk Shift
            sprintf(*attributeName, "Disk Shift");
            break;
        case 221: //G-Sense Error Rate
            sprintf(*attributeName, "G-Sense Error Rate");
            break;
        case 222: //Loaded Hours
            sprintf(*attributeName, "Loaded Hours");
            break;
        case 223: //Load/Unload Retry Count
            sprintf(*attributeName, "Load/Unload Retry Count");
            break;
        case 224: //Load Friction
            sprintf(*attributeName, "Load Friction");
            break;
        case 225: //Load/Unload Cycle Count
            sprintf(*attributeName, "Load/Unload Cycle Count");
            break;
        case 226: //Load-in Time
            sprintf(*attributeName, "Load-In Time");
            break;
        case 227: //Torque Amplification Count
            sprintf(*attributeName, "Torgque Amplification Count");
            break;
        case 228: //Power-Off Retract Cycle
            sprintf(*attributeName, "Power-Off Retract Cycle");
            break;
        case 230: //GMR Head Amplitude
            sprintf(*attributeName, "GMR Head Amplitude");
            break;
        case 231: //Temperature
            sprintf(*attributeName, "Temperature");
            break;
        case 240: //Head Flying Hours
            sprintf(*attributeName, "Head Flying Hours");
            break;
        case 250: //Read Error Retry Rate
            sprintf(*attributeName, "Read Error Retry Rate");
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
            sprintf(*attributeName, "Raw Read Error Rate");
            break;
        case 5://retired block count
            sprintf(*attributeName, "Retired Block Count");
            break;
        case 9: //Power on Hours
            sprintf(*attributeName, "Power On Hours");
            break;
        case 12: //Drive Power Cycle Count
            sprintf(*attributeName, "Drive Power Cycle Count");
            break;
        case 100: //Total Erase Count
            sprintf(*attributeName, "Total Erase Count");
            break;
        case 168: //Min Power Cycle Count
            sprintf(*attributeName, "Min Power Cycle Count");
            break;
        case 169: //Max power cycle count (seagate-vendor-b-c)
            sprintf(*attributeName, "Max Power Cycle Count");
            break;
        case 171: //Program Fail Count
            sprintf(*attributeName, "Program Fail Count");
            break;
        case 172: //Erase Fail Count
            sprintf(*attributeName, "Erase Fail Count");
            break;
        case 174: //Unexpected Power Loss Count
            sprintf(*attributeName, "Unexpected Power Loss Count");
            break;
        case 175: //Maximum Program Fail Count
            sprintf(*attributeName, "Maximum Program Fail Count");
            break;
        case 176: //Maximum Erase Fail Count
            sprintf(*attributeName, "Maximum Erase Fail Count");
            break;
        case 177: //Wear Leveling Count
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "Wear Leveling Count");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "Endurance Used");
            }
            break;
        case 178: //Used Reserved Block Count for The Worst Die
            sprintf(*attributeName, "Used Reserve Block Count (Chip)");
            break;
        case 179: //Used Reserved Block Count for SSD
            sprintf(*attributeName, "Used Reserve Block Count (Total)");
            break;
        case 180: //reported IOEDC Error In Interval (Seagate/Samsung), End to End Error Detection Rate
            if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "End To End Error Detection Rate");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "Unused Reserved Block Count (Total)");
            }
            break;
        case 181: //Program Fail Count
            sprintf(*attributeName, "Program Fail Count");
            break;
        case 182: //Erase Fail Count
            sprintf(*attributeName, "Erase Fail Count");
            break;
        case 183://PHY Counter Events (Seagate), SATA Downshift Count (Seagate-vendor-b-c)
            sprintf(*attributeName, "SATA Downshift Count");
            break;
        case 184://IOEDC Count (Seagate), End to End Error Detection Count (Seagate-vendor-b-c)
            sprintf(*attributeName, "End To End Error Detection Count");
            break;
        case 187: //Reported Un-correctable
            sprintf(*attributeName, "Reported Un-correctable");
            break;
        case 188: //Command Timeout
            sprintf(*attributeName, "Command Timeout");
            break;
        case 190: //Airflow Temperature (Seagate), SATA Error Counters (Seagate-vendor-b-c)
            sprintf(*attributeName, "SATA Error Counters");
            break;
        case 194: //Temperature
            sprintf(*attributeName, "Temperature");
            break;
        case 195: //ECC On the Fly Count (Seagate)
            sprintf(*attributeName, "ECC On The Fly Count");
            break;
        case 196: //Re-allocate Sector Event
            sprintf(*attributeName, "Re-allocate Sector Event");
            break;
        case 197: //Pending-Sparing Count
            sprintf(*attributeName, "Current Pending Sector Count");
            break;
        case 198://offlince uncorrectable sectors
            sprintf(*attributeName, "Off-line Uncorrectable Sectors");
            break;
        case 199: //Ultra DMA CRC Error
            sprintf(*attributeName, "Ultra DMA CRC Error");
            break;
        case 201: //Uncorrectable Read Error Rate (Seagate-vendor-b-c)
            sprintf(*attributeName, "Uncorrectable Read Error Rate");
            break;
        case 204: //Soft ECC Correction Rate
            sprintf(*attributeName, "Soft ECC Correction Rate");
            break;
        case 212: //Phy Error Count
            sprintf(*attributeName, "Phy Error Count");
            break;
        case 231: //SSD Life Left
            sprintf(*attributeName, "SSD Life Left");
            break;
        case 234: //
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "NAND GiB Written");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "Vendor Specific");
            }
            break;
        case 241: //Lifetime Writes from Host
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "Lifetime Writes From Host in GiB");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "Total LBAs Written");
            }
            break;
        case 242: //Lifetime Reads from Host
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "Lifetime Reads From Host in GiB");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "Total LBAs Read");
            }
            break;
        case 245: //SSD Life Left (%)
            sprintf(*attributeName, "SSD Life Left %%");
            break;
        case 250: //Lifetime NAND Read Retries
            if (isSeagateDrive == SEAGATE_VENDOR_B)
            {
                sprintf(*attributeName, "Lifetime NAND Read Retries");
            }
            else if (isSeagateDrive == SEAGATE_VENDOR_C)
            {
                sprintf(*attributeName, "Read Error Retry Rate");
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
    sprintf(attributeName, "                             ");
    return;
}

static void print_Raw_ATA_Attributes(tDevice *device, smartLogData *smartData)
{
    //making the attribute name seperate so that if we add is_Seagate() logic in we can turn on and off printing the name
    char *attributeName = (char *)calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char));
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
    safe_Free(attributeName);
}

static void print_Analyzed_ATA_Attributes(tDevice *device, smartLogData *smartData)
{
    //making the attribute name seperate so that if we add is_Seagate() logic in we can turn on and off printing the name
    char *attributeName = (char *)calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char));
    if (attributeName == NULL)
    {
        perror("Calloc Failure!\n");
        return;
    }

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
                printf("\tRaw Data: ");
                for (uint8_t rawIter = 0; rawIter < 7; ++rawIter)
                {
                    printf("%02"PRIX8"", smartData->attributes.ataSMARTAttr.attributes[iter].data.rawData[6 - rawIter]);
                }
                printf("h\n");
            }
        }
    }
    safe_Free(attributeName);
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
        bool readSCTStatusWithSMARTCommand = sct_With_SMART_Commands(device);//USB hack
        uint8_t sctStatus[512] = { 0 };
        if (device->drive_info.ata_Options.generalPurposeLoggingSupported && !readSCTStatusWithSMARTCommand &&
            SUCCESS == ata_Read_Log_Ext(device, ATA_SCT_COMMAND_STATUS, 0, sctStatus, 512, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0)
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
        if (supports_ATA_Return_SMART_Status_Command(device))//USB hack. Will return true on IDE/SCSI interface. May return true or false otherwise depending on what device we detect
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
        else
        {
            //this device doesn't support getting SMART status from return status command (translator bug)
            //try other methods.
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
                                    char *attributeName = (char *)calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char));
                                    get_Attribute_Name(device, tripInfo->ataAttribute.attributeNumber, &attributeName);
                                    if (strlen(attributeName))
                                    {
                                        //use the name in the error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "%s [%" PRIu8 "] set to test trip!", attributeName, tripInfo->ataAttribute.attributeNumber);
                                        tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
                                    }
                                    else
                                    {
                                        //Couldn't look up the name, so set a generic error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "Attribute %" PRIu8 " set to test trip!", tripInfo->ataAttribute.attributeNumber);
                                        tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
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
                                    char *attributeName = (char *)calloc(MAX_ATTRIBUTE_NAME_LENGTH, sizeof(char));
                                    get_Attribute_Name(device, tripInfo->ataAttribute.attributeNumber, &attributeName);
                                    if (strlen(attributeName))
                                    {
                                        //use the name in the error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "%s [%" PRIu8 "] tripped! Nominal Value %" PRIu8 " below Threshold %" PRIu8 "", attributeName, tripInfo->ataAttribute.attributeNumber, tripInfo->ataAttribute.nominalValue, tripInfo->ataAttribute.thresholdValue);
                                        tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
                                    }
                                    else
                                    {
                                        //Couldn't look up the name, so set a generic error reason
                                        snprintf(tripInfo->reasonString, UINT8_MAX, "Attribute %" PRIu8 " tripped! Nominal Value %" PRIu8 " below Threshold %" PRIu8 "", tripInfo->ataAttribute.attributeNumber, tripInfo->ataAttribute.nominalValue, tripInfo->ataAttribute.thresholdValue);
                                        tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
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

void translate_SCSI_SMART_Sense_To_String(uint8_t asc, uint8_t ascq, char *reasonString, uint8_t reasonStringMaxLength)
{
    switch (asc)
    {
    case 0x5D:
        if (ascq >= 0 && ascq < 0x10)
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
}
//
int scsi_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
{
    int ret = NOT_SUPPORTED;
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
                    translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX);
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
                    translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX);
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
        uint8_t *senseData = (uint8_t*)calloc(SPC3_SENSE_LEN, sizeof(uint8_t));
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
                translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX);
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
                translate_SCSI_SMART_Sense_To_String(tripInfo->scsiSenseCode.asc, tripInfo->scsiSenseCode.ascq, tripInfo->reasonString, UINT8_MAX);
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
        safe_Free(senseData);
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
    smartPageOpts.addr = (uint64_t)smartLogPage;
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
                sprintf(tripInfo->reasonString, "Available Spare Space has fallen below the threshold\0");
                tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
            }
            if (smartLogPage[0] & BIT1)
            {
                tripInfo->nvmeCriticalWarning.temperatureExceedsThreshold = true;
                sprintf(tripInfo->reasonString, "Temperature is above an over termperature threshold or below an under temperature threshold\0");
                tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
            }
            if (smartLogPage[0] & BIT2)
            {
                tripInfo->nvmeCriticalWarning.nvmSubsystemDegraded = true;
                sprintf(tripInfo->reasonString, "NVM subsystem reliability has been degraded due to significant media related errors or an internal error that degrades reliability\0");
                tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
            }
            if (smartLogPage[0] & BIT3)
            {
                tripInfo->nvmeCriticalWarning.mediaReadOnly = true;
                sprintf(tripInfo->reasonString, "Media has been placed in read only mode\0");
                tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
            }
            if (smartLogPage[0] & BIT4)
            {
                tripInfo->nvmeCriticalWarning.volatileMemoryBackupFailed = true;
                sprintf(tripInfo->reasonString, "Volatile Memory backup device has failed\0");
                tripInfo->reasonStringLength = (uint8_t)strlen(tripInfo->reasonString);
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
#endif
    case SCSI_DRIVE:
    {
        //read the informational exceptions mode page and check MRIE value for something other than 0
        uint8_t *infoExceptionsControl = (uint8_t*)calloc(12 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
        safe_Free(infoExceptionsControl);
    }
    break;
    default:
        break;
    }
    return enabled;
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
            if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, rotatingMediaStatistics, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
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
            if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, rotatingMediaStatistics, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
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
                featureCode = (uint16_t)sctFeature;
                state = hdaTemperatureIntervalOrState;
                break;
            }
            //set option flags
            if (!isVolatile)
            {
                optionFlags = BIT0;
            }
            ret = ata_SCT_Feature_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0001, featureCode, &state, &optionFlags);
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
                featureCode = (uint16_t)sctFeature;
                break;
            }
            ret = ata_SCT_Feature_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0002, featureCode, &state, &optionFlags);
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
                    ret = ata_SCT_Feature_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0003, featureCode, &state, &optionFlags);
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
            //made it this far, so the feature is supported
            switch (ercCommand)
            {
            case SCT_ERC_READ_COMMAND:
                ret = ata_SCT_Error_Recovery_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0001, 0x0001, NULL, timerValueMilliseconds / 100);
                break;
            case SCT_ERC_WRITE_COMMAND:
                ret = ata_SCT_Error_Recovery_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0001, 0x0002, NULL, timerValueMilliseconds / 100);
                break;
            default:
                break;
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
                ret = ata_SCT_Error_Recovery_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0002, 0x0001, &currentTimerValue, 0);
                break;
            case SCT_ERC_WRITE_COMMAND:
                ret = ata_SCT_Error_Recovery_Control(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0x0002, 0x0002, &currentTimerValue, 0);
                break;
            default:
                break;
            }
            if (ret == SUCCESS)
            {
                *timerValueMilliseconds = (uint32_t)currentTimerValue * UINT32_C(100);
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
        uint8_t *infoLogPage = (uint8_t*)calloc(LP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t));
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
            safe_Free(infoLogPage);
        }
    }
    //read the mode page
    uint8_t *infoControlPage = (uint8_t*)calloc(MODE_PARAMETER_HEADER_10_LEN + MP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t));
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
        safe_Free(infoControlPage);
    }
    return ret;
}

int set_SCSI_Informational_Exceptions_Info(tDevice *device, bool save, ptrInformationalExceptionsControl controlData)
{
    int ret = SUCCESS;
    uint8_t *infoControlPage = (uint8_t*)calloc(MODE_PARAMETER_HEADER_10_LEN + MP_INFORMATION_EXCEPTIONS_LEN, sizeof(uint8_t));
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
        ret = scsi_Mode_Select_6(device, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN, true, save, infoControlPage, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN);
    }
    else
    {
        ret = scsi_Mode_Select_10(device, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN, true, save, infoControlPage, modePageDataOffset + MP_INFORMATION_EXCEPTIONS_LEN);
    }
    safe_Free(infoControlPage);
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
            if ((smartInfo->offlineDataCollectionStatus >= 0x40 && smartInfo->offlineDataCollectionStatus <= 0x7F) || (smartInfo->offlineDataCollectionStatus >= 0xC0 && smartInfo->offlineDataCollectionStatus <= 0xFF))
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
                        if (errorLogIndex > 0 && errorLogIndex < 5)
                        {
                            uint8_t zeros[90] = { 0 };
                            uint32_t offset = 2 + (errorLogIndex * 90);//first entry is at offset 2, each entry is 90 bytes long
                            //offset should now be our starting point to populate the list
                            for (uint8_t entryCount = 0; entryCount < M_Min(5, smartErrorLog->deviceErrorCount); ++entryCount, offset += 90)
                            {
                                if (offset > 451)
                                {
                                    offset = 2;
                                    //log has wrapped, restart to the first entry
                                }
                                //check if the entry is empty
                                if (memcmp(&errorLog[offset], zeros, 90) == 0)
                                {
                                    //restart the loop to find another entry (if any)
                                    continue;
                                }
                                //each entry has 5 command data structures to fill in followed by error data
                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extDataStructures = false;
                                //NOTE: don't memcpy since we aren't packing the structs
                                uint32_t commandEntryOffset = offset;
                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += 12)
                                {
                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, 12) == 0)
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

//This function will automatically select SMART vs GPL log
int get_ATA_Comprehensive_SMART_Error_Log(tDevice * device, ptrComprehensiveSMARTErrorLog smartErrorLog)
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
                //now check for GPL summort so we know if we are reading the ext log or not
                if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
                {
                    //extended comprehensive SMART error log
                    //We will read each sector of the log as we need it to help with some USB compatibility (and so we don't read more than we need)
                    uint8_t errorLog[512] = { 0 };
                    uint16_t pageNumber = 0;
                    uint32_t compErrLogSize = 0;
                    get_ATA_Log_Size(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, &compErrLogSize, false, true);
                    uint16_t maxPage = compErrLogSize / 512;
                    if (compErrLogSize > 0)
                    {
                        int getLog = ata_Read_Log_Ext(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, pageNumber, errorLog, 512, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0);
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
                            smartErrorLog->deviceErrorCount = M_BytesTo2ByteValue(errorLog[501], errorLog[500]);
                            uint16_t errorLogIndex = M_BytesTo2ByteValue(errorLog[3], errorLog[2]);
                            if (errorLogIndex > 0)
                            {
                                //get the starting page number
                                pageNumber = errorLogIndex / 4;//4 entries per page
                                uint8_t pageEntryNumber = errorLogIndex % 4;//which entry on the page (zero indexed)
                                uint8_t zeros[124] = { 0 };
                                while (smartErrorLog->numberOfEntries < SMART_EXT_COMPREHENSIVE_ERRORS_MAX && smartErrorLog->numberOfEntries < smartErrorLog->deviceErrorCount)
                                {
                                    while (pageNumber < maxPage)
                                    {
                                        //first read this page
                                        memset(errorLog, 0, 512);
                                        getLog = ata_Read_Log_Ext(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, pageNumber, errorLog, 512, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0);
                                        if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                                        {
                                            if (getLog == WARN_INVALID_CHECKSUM)
                                            {
                                                smartErrorLog->checksumsValid = false;
                                            }
                                            while (pageEntryNumber < 4)
                                            {
                                                uint32_t offset = (pageEntryNumber * 124) + 4;
                                                ++pageEntryNumber;//increment now before we forget
                                                //check if the entry is empty
                                                if (memcmp(&errorLog[offset], zeros, 124) == 0)
                                                {
                                                    //restart the loop to find another entry (if any)
                                                    continue;
                                                }
                                                //each entry has 5 command data structures to fill in followed by error data
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extDataStructures = true;
                                                //NOTE: don't memcpy since we aren't packing the structs
                                                uint32_t commandEntryOffset = offset;
                                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += 18)
                                                {
                                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, 18) == 0)
                                                    {
                                                        continue;
                                                    }
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].deviceControl = errorLog[commandEntryOffset + 0];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].feature = errorLog[commandEntryOffset + 1];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].featureExt = errorLog[commandEntryOffset + 2];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].count = errorLog[commandEntryOffset + 3];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].countExt = errorLog[commandEntryOffset + 4];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaLow = errorLog[commandEntryOffset + 5];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaLowExt = errorLog[commandEntryOffset + 6];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaMid = errorLog[commandEntryOffset + 7];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaMidExt = errorLog[commandEntryOffset + 8];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaHi = errorLog[commandEntryOffset + 9];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].lbaHiExt = errorLog[commandEntryOffset + 10];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].device = errorLog[commandEntryOffset + 11];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].contentWritten = errorLog[commandEntryOffset + 12];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].reserved = errorLog[commandEntryOffset + 13];
                                                    smartErrorLog->smartError[smartErrorLog->numberOfEntries].extCommand[commandEntry].timestampMilliseconds = M_BytesTo4ByteValue(errorLog[commandEntryOffset + 17], errorLog[commandEntryOffset + 16], errorLog[commandEntryOffset + 15], errorLog[commandEntryOffset + 14]);
                                                    ++(smartErrorLog->smartError[smartErrorLog->numberOfEntries].numberOfCommands);
                                                }
                                                //now set the error data
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.transportSpecific = errorLog[offset + 90];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.error = errorLog[offset + 91];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.count = errorLog[offset + 92];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.countExt = errorLog[offset + 93];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaLow = errorLog[offset + 94];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaLowExt = errorLog[offset + 95];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaMid = errorLog[offset + 96];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaMidExt = errorLog[offset + 97];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaHi = errorLog[offset + 98];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lbaHiExt = errorLog[offset + 99];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.device = errorLog[offset + 100];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.status = errorLog[offset + 101];
                                                memcpy(smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.extendedErrorInformation, &errorLog[offset + 102], 19);
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.state = errorLog[offset + 121];
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extError.lifeTimestamp = M_BytesTo2ByteValue(errorLog[offset + 123], errorLog[offset + 122]);
                                                ++(smartErrorLog->numberOfEntries);
                                            }
                                            //reset the pageEntry number to zero for next page
                                            pageEntryNumber = 0;
                                        }
                                        else
                                        {
                                            //break out of the loop!
                                        }
                                        ++pageNumber;//go to the next page of the log
                                    }
                                    //roll page number back to beginning
                                    pageNumber = 0;
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
                else
                {
                    //comprehensive SMART error log
                    //read the first sector to get index and device error count. Will read the full thing if those are non-zero
                    uint32_t compErrLogSize = 0;
                    get_ATA_Log_Size(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, &compErrLogSize, false, true);
                    if (compErrLogSize > 0)
                    {
                        uint8_t *errorLog = (uint8_t*)calloc(512, sizeof(uint8_t));
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
                                uint8_t *temp = (uint8_t*)realloc(errorLog, compErrLogSize * sizeof(uint8_t));
                                if (!temp)
                                {
                                    safe_Free(errorLog);
                                    return MEMORY_FAILURE;
                                }
                                errorLog = temp;
                                memset(errorLog, 0, compErrLogSize);
                                getLog = ata_SMART_Read_Log(device, ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG, errorLog, compErrLogSize);
                                if (getLog == SUCCESS || getLog == WARN_INVALID_CHECKSUM)
                                {
                                    //We now have the full log in memory. 
                                    //First, figure out the first page to read. Next: need to handle switching between pages as we fill in the structure with data.
                                    uint16_t pageNumber = errorLogIndex / 5;//5 entries per page
                                    uint16_t maxPages = compErrLogSize / 512;
                                    //byte offset, this will point to the first entry
                                    uint8_t pageEntryNumber = errorLogIndex % 5;//remainder...zero indexed
                                    uint32_t offset = 0;// (pageNumber * 512) + (pageEntryNumber * 90) + 2;
                                    //EX: Entry 28: pageNumber = 28 / 5 = 5;
                                    //              pageEntryNumber = 28 % 5 = 3;
                                    //              offset = (5 * 512) + (3 * 90) + 2;
                                    //              5 * 512 gets us to that page offset (2560)
                                    //              3 * 90 + 2 gets us to the entry offset on the page we need = 272, which is 4th entry on the page (5th page)
                                    //              this gets us entry 4 on page 5 which is entry number 28
                                    //Now, we need to loop through the data and jump between pages.
                                    //go until we fill up our structure with a max number of entries
                                    uint8_t zeros[90] = { 0 };
                                    while (smartErrorLog->numberOfEntries < SMART_COMPREHENSIVE_ERRORS_MAX && smartErrorLog->numberOfEntries < smartErrorLog->deviceErrorCount)
                                    {
                                        while (pageNumber < maxPages)
                                        {
                                            while (pageEntryNumber < 5)
                                            {
                                                //calculate the offset of the first entry we need to read from this page
                                                offset = (pageNumber * 512) + (pageEntryNumber * 90) + 2;
                                                ++pageEntryNumber;//increment now so we don't forget to in this loop
                                                //read the entry into memory if it is valid, otherwise continue the loop
                                                //check if the entry is empty
                                                if (memcmp(&errorLog[offset], zeros, 90) == 0)
                                                {
                                                    //restart the loop to find another entry (if any)
                                                    continue;
                                                }
                                                //each entry has 5 command data structures to fill in followed by error data
                                                smartErrorLog->smartError[smartErrorLog->numberOfEntries].extDataStructures = false;
                                                //NOTE: don't memcpy since we aren't packing the structs
                                                uint32_t commandEntryOffset = offset;
                                                for (uint8_t commandEntry = 0; commandEntry < 5; ++commandEntry, commandEntryOffset += 12)
                                                {
                                                    if (memcmp(&errorLog[commandEntryOffset + 0], zeros, 12) == 0)
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
                                            }
                                            pageEntryNumber = 0;//back to first entry for the next page
                                            ++pageNumber;
                                        }
                                        pageNumber = 0;//if we broke out of the previous loop, we hit the end and need to go back to the first page
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
                        safe_Free(errorLog);
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
    bool longCmd = false;//read/write long
    bool fpdma = false;
    //bool noRetries = false;
    uint64_t commandLBA = lba;
    uint32_t sectorsToTransfer = count;//true for synchronous commands...
    switch (commandOpCode)
    {
    case ATA_WRITE_LONG_NORETRY:
    case ATA_READ_LONG_NORETRY:
        //noRetries = true;
    case ATA_READ_LONG_RETRY:
    case ATA_WRITE_LONG_RETRY:
        longCmd = true;
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
    case ATA_READ_DMA_QUE_EXT:
    case ATA_WRITE_DMA_QUE_FUA_EXT:
    case ATA_WRITE_DMA_QUE_EXT:
        ext = true;
    case ATA_WRITE_DMA_QUEUED_CMD:
    case ATA_READ_DMA_QUEUED_CMD:
        async = true;
        sectorsToTransfer = features;//number of sectors to tansfer
        break;
    default://unknown command...
        return;
        break;
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
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu64 " Count: %" PRIu32 " NCQ Tag: %" PRIu8 " FUA: %d PRIO: %" PRIu8 "", commandName, lba, sectorsToTransfer, tag, forceUnitAccess, prio);
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
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu32 " Count: %" PRIu16 " Tag: %" PRIu8 "", commandName, readSecLBA, sectorsToTransfer, tag);
            }
            else
            {
                uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                uint8_t head = M_Nibble0(device);
                uint8_t sector = M_Byte0(lba);
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu16 " Tag: %" PRIu8 "", commandName, cylinder, head, sector, sectorsToTransfer, tag);
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
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - LBA: %" PRIu32 " Count: %" PRIu16 "", commandName, readSecLBA, sectorsToTransfer);
            }
            else
            {
                uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                uint8_t head = M_Nibble0(device);
                uint8_t sector = M_Byte0(lba);
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu16 "", commandName, cylinder, head, sector, sectorsToTransfer);
            }
        }
    }
}

void get_GPL_Log_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
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
            snprintf(logAddressName, 31, "Host Specific");
        }
        else if (logAddress >= 0xA0 && logAddress <= 0xDF)
        {
            snprintf(logAddressName, 31, "Vendor Specific");
        }
        else
        {
            snprintf(logAddressName, 31, "Unknown");
        }
        break;
    }
    if (invalidLog)
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Log: %s (Invalid Address) Page Number: %" PRIu16 " PageCount: %" PRIu16 " Features: %" PRIX16 "h", commandName, logAddressName, pageNumber, logPageCount, features);
    }
    else
    {
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Log: %s Page Number: %" PRIu16 " PageCount: %" PRIu16 " Features: %" PRIX16 "h", commandName, logAddressName, pageNumber, logPageCount, features);
    }
}

void get_Download_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
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
    char securityProtocolName[21] = { 0 };
    switch (securityProtocol)
    {
    case SECURITY_PROTOCOL_RETURN_SUPPORTED:
        snprintf(securityProtocolName, 20, "Supported");
        break;
    case SECURITY_PROTOCOL_TCG_1:
    case SECURITY_PROTOCOL_TCG_2:
    case SECURITY_PROTOCOL_TCG_3:
    case SECURITY_PROTOCOL_TCG_4:
    case SECURITY_PROTOCOL_TCG_5:
    case SECURITY_PROTOCOL_TCG_6:
        snprintf(securityProtocolName, 20, "TCG %" PRIu8 "", securityProtocol);
        break;
    case SECURITY_PROTOCOL_CbCS:
        snprintf(securityProtocolName, 20, "CbCS");
        break;
    case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
        snprintf(securityProtocolName, 20, "Tape Encryption");
        break;
    case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
        snprintf(securityProtocolName, 20, "Encryption Configuration");
        break;
    case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
        snprintf(securityProtocolName, 20, "SA Creation Cap");
        break;
    case SECURITY_PROTOCOL_IKE_V2_SCSI:
        snprintf(securityProtocolName, 20, "IKE V2 SCSI");
        break;
    case SECURITY_PROTOCOL_NVM_EXPRESS:
        snprintf(securityProtocolName, 20, "NVM Express");
        break;
    case SECURITY_PROTOCOL_SCSA:
        snprintf(securityProtocolName, 20, "SCSA");
        break;
    case SECURITY_PROTOCOL_JEDEC_UFS:
        snprintf(securityProtocolName, 20, "JEDEC UFS");
        break;
    case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
        snprintf(securityProtocolName, 20, "SD Trusted Flash");
        break;
    case SECURITY_PROTOCOL_IEEE_1667:
        snprintf(securityProtocolName, 20, "IEEE 1667");
        break;
    case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
        snprintf(securityProtocolName, 20, "ATA Security");
        break;
    default:
        if (securityProtocol >= 0xF0 && securityProtocol <= 0xFF)
        {
            snprintf(securityProtocolName, 20, "Vendor Specific (%02" PRIX8"h)", securityProtocol);
            break;
        }
        else
        {
            snprintf(securityProtocolName, 20, "Unknown (%02" PRIX8"h)", securityProtocol);
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

void get_SMART_Offline_Immediate_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH], const char *smartSigValid)
{
    uint8_t offlineImmdTest = M_Byte0(lba);
    char offlineTestName[31] = { 0 };
    switch (offlineImmdTest)
    {
    case 0://SMART off-line routine (offline mode)
        snprintf(offlineTestName, 30, "SMART Off-line routine");
        break;
    case 0x01://short self test (offline)
        snprintf(offlineTestName, 30, "Short Self-Test (offline)");
        break;
    case 0x02://extended self test (offline)
        snprintf(offlineTestName, 30, "Extended Self-Test (offline)");
        break;
    case 0x03://conveyance self test (offline)
        snprintf(offlineTestName, 30, "Conveyance Self-Test (offline)");
        break;
    case 0x04://selective self test (offline)
        snprintf(offlineTestName, 30, "Selective Self-Test (offline)");
        break;
    case 0x7F://abort offline test
        snprintf(offlineTestName, 30, "Abort Self-Test");
        break;
    case 0x81://short self test (captive)
        snprintf(offlineTestName, 30, "Short Self-Test (captive)");
        break;
    case 0x82://extended self test (captive)
        snprintf(offlineTestName, 30, "Extended Self-Test (captive)");
        break;
    case 0x83://conveyance self test (captive)
        snprintf(offlineTestName, 30, "Conveyance Self-Test (captive)");
        break;
    case 0x84://selective self test (captive)
        snprintf(offlineTestName, 30, "Selective Self-Test (captive)");
        break;
    default:
        if (offlineImmdTest >= 0x05 && offlineImmdTest <= 0x3F)
        {
            //reserved (offline)
            snprintf(offlineTestName, 30, "Unknown %" PRIX8 "h (offline)", offlineImmdTest);
        }
        else if (offlineImmdTest == 0x80 || (offlineImmdTest >= 0x85 && offlineImmdTest <= 0x8F))
        {
            //reserved (captive)
            snprintf(offlineTestName, 30, "Unknown %" PRIX8 "h (captive)", offlineImmdTest);
        }
        else if (offlineImmdTest >= 0x40 && offlineImmdTest <= 0x7E)
        {
            //vendor unique (offline)
            snprintf(offlineTestName, 30, "Vendor Specific %" PRIX8 "h (offline)", offlineImmdTest);
        }
        else if (offlineImmdTest >= 0x90 && offlineImmdTest <= 0xFF)
        {
            //vendor unique (captive)
            snprintf(offlineTestName, 30, "Vendor Specific %" PRIX8 "h (captive)", offlineImmdTest);
        }
        else
        {
            //shouldn't get here, but call it a generic unknown self test
            snprintf(offlineTestName, 30, "Unknown %" PRIX8 "h", offlineImmdTest);
        }
        break;
    }
    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Offline Immediate: %s, SMART Signature %s", commandName, offlineTestName, smartSigValid);
}

void get_SMART_Log_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH], const char *smartSigValid)
{
    uint8_t logAddress = M_Byte0(lba);
    char logAddressName[31] = { 0 };
    uint8_t logPageCount = M_Byte0(count);
    bool invalidLog = false;
    switch (logAddress)
    {
    case ATA_LOG_DIRECTORY:
        snprintf(logAddressName, 30, "Directory");
        break;
    case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
        snprintf(logAddressName, 30, "Summary SMART Error");
        break;
    case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
        snprintf(logAddressName, 30, "Comprehensive SMART Error");
        break;
    case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Ext Comprehensive SMART Error");
        invalidLog = true;
        break;
    case ATA_LOG_DEVICE_STATISTICS:
        snprintf(logAddressName, 30, "Device Statistics");
        break;
    case ATA_LOG_SMART_SELF_TEST_LOG:
        snprintf(logAddressName, 30, "SMART Self-Test");
        break;
    case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Ext SMART Self-Test");
        invalidLog = true;
        break;
    case ATA_LOG_POWER_CONDITIONS://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Power Conditions");
        invalidLog = true;
        break;
    case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
        snprintf(logAddressName, 30, "Selective Self-Test");
        break;
    case ATA_LOG_DEVICE_STATISTICS_NOTIFICATION://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Device Statistics Notification");
        invalidLog = true;
        break;
    case ATA_LOG_PENDING_DEFECTS_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Pending Defects");
        invalidLog = true;
        break;
    case ATA_LOG_LPS_MISALIGNMENT_LOG:
        snprintf(logAddressName, 30, "LPS Misalignment");
        break;
    case ATA_LOG_SENSE_DATA_FOR_SUCCESSFUL_NCQ_COMMANDS://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Sense Data for Successful NCQ");
        invalidLog = true;
        break;
    case ATA_LOG_NCQ_COMMAND_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "NCQ Command Errors");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "SATA Phy Event Counters");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "NCQ Queue Management");
        invalidLog = true;
        break;
    case ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "NCQ Send and Receive");
        invalidLog = true;
        break;
    case ATA_LOG_HYBRID_INFORMATION://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Hybrid Information");
        invalidLog = true;
        break;
    case ATA_LOG_REBUILD_ASSIST://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Rebuild Assist");
        invalidLog = true;
        break;
    case ATA_LOG_LBA_STATUS://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "LBA Status");
        invalidLog = true;
        break;
    case ATA_LOG_STREAMING_PERFORMANCE://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Streaming Performance");
        invalidLog = true;
        break;
    case ATA_LOG_WRITE_STREAM_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Write Stream Errors");
        invalidLog = true;
        break;
    case ATA_LOG_READ_STREAM_ERROR_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Read Stream Errors");
        invalidLog = true;
        break;
    case ATA_LOG_DELAYED_LBA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Delayed LBA");
        invalidLog = true;
        break;
    case ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Current Device Internal Status");
        invalidLog = true;
        break;
    case ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Saved Device Internal Status");
        invalidLog = true;
        break;
    case ATA_LOG_SECTOR_CONFIGURATION_LOG://GPL log...should be an error using this command!
        snprintf(logAddressName, 30, "Sector Configuration");
        invalidLog = true;
        break;
    case ATA_LOG_IDENTIFY_DEVICE_DATA:
        snprintf(logAddressName, 30, "Identify Device Data");
        break;
    case ATA_SCT_COMMAND_STATUS:
        snprintf(logAddressName, 30, "SCT Command/Status");
        break;
    case ATA_SCT_DATA_TRANSFER:
        snprintf(logAddressName, 30, "SCT Data Transfer");
        break;
    default:
        if (logAddress >= 0x80 && logAddress <= 0x9F)
        {
            snprintf(logAddressName, 30, "Host Specific");
        }
        else if (logAddress >= 0xA0 && logAddress <= 0xDF)
        {
            snprintf(logAddressName, 30, "Vendor Specific");
        }
        else
        {
            snprintf(logAddressName, 30, "Unknown");
        }
        break;
    }
    if (invalidLog)
    {
        if (M_Byte0(features) == 0xD5)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Read Log - Log: %s (Invalid Address) PageCount: %" PRIu8 "", commandName, logAddressName, logPageCount);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Write Log - Log: %s (Invalid Address) PageCount: %" PRIu8 "", commandName, logAddressName, logPageCount);
        }
    }
    else
    {
        if (M_Byte0(features) == 0xD5)
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Read Log- Log: %s PageCount: %" PRIu8 "", commandName, logAddressName, logPageCount);
        }
        else
        {
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s Write Log- Log: %s PageCount: %" PRIu8 "", commandName, logAddressName, logPageCount);
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
        get_SMART_Offline_Immediate_Info(commandName, commandOpCode, features, count, lba, device, commandInfo, (const char*)smartSigValid);
        break;
    case ATA_SMART_READ_LOG:
    case ATA_SMART_WRITE_LOG:
        get_SMART_Log_Info(commandName, commandOpCode, features, count, lba, device, commandInfo, (const char*)smartSigValid);
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
        if ((subcommand >= 0x00 && subcommand <= 0xCF)
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

void get_Sanitize_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint16_t subcommand = features;
    uint32_t signature = M_DoubleWord0(lba);//TODO: may need to byte swap this //NOTE: for overwrite, this is the pattern. 47:32 contain a signature
    bool zoneNoReset = count & BIT15;
    bool invertBetweenPasses = count & BIT7;//overwrite only
    bool definitiveEndingPattern = count & BIT6;//overwrite only
    bool failure = count & BIT4;
    bool clearSanitizeOperationFailed = count & BIT0;//status only
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
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Overwrite Erase, ZNR: %d, Invert: %d, Definitive Pattern: %d, Failure Mode: %d, Passes: %" PRIu8 ", Pattern: %08" PRIX8 "h, Signature %s", commandName, zoneNoReset, invertBetweenPasses, definitiveEndingPattern, failure, overwritePasses, overwritePattern, sanitizeSignatureValid);
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

void get_DCO_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
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
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%02" PRIX8 "h), LBA = %07" PRIX32 "h, Count = %02" PRIX8 "h", commandName, subcommand, (uint32_t)lba, (uint8_t)count);
        break;
    }
}

void get_Set_Max_Address_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
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
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu32 " (Volatile)", commandName, lba);
            }
            else
            {
                snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Maximum LBA: %" PRIu32 "", commandName, lba);
            }
            break;
        case HPA_SET_MAX_PASSWORD:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Password", commandName, lba);
            break;
        case HPA_SET_MAX_LOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Lock", commandName, lba);
            break;
        case HPA_SET_MAX_UNLOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unlock", commandName, lba);
            break;
        case HPA_SET_MAX_FREEZE_LOCK:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Freeze Lock", commandName, lba);
            break;
        case HPA_SET_MAX_PASSWORD_DMA:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Set Password DMA", commandName, lba);
            break;
        case HPA_SET_MAX_UNLOCK_DMA:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unlock DMA", commandName, lba);
            break;
        default:
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%02" PRIX8 "h), LBA = %07" PRIX32 "h, Count = %02" PRIX8 "h", commandName, subcommand, (uint32_t)lba, (uint8_t)count);
            break;
        }
    }
}

//Only idle and standby...not the immediate commands!
void get_Idle_Or_Standby_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
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
            snprintf(standbyTimerPeriodString, 30, "Unknown Timer Value (%02" PRIX8 "h)");
        }
        break;
    }
    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Standby Timer Period: %s", commandName, standbyTimerPeriodString);
}

void get_NV_Cache_Command_Info(const char* commandName, uint8_t commandOpCode, uint16_t features, uint16_t count, uint64_t lba, uint8_t device, char commandInfo[ATA_COMMAND_INFO_MAX_LENGTH])
{
    uint16_t subcommand = features;
    char subCommandName[31] = { 0 };
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
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Vendor Specific (%04" PRIX16 "h), LBA = %012" PRIX32 "h, Count = %04" PRIX8 "h", commandName, subcommand, lba, count);
        }
        else
        {
            //reserved for NV cache feature
            snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "%s - Unknown (%04" PRIX16 "h), LBA = %012" PRIX32 "h, Count = %04" PRIX8 "h", commandName, subcommand, lba, count);
        }
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
			snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "NOP (subcommand %02" PRIx8 "h", (uint8_t)features);
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
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Zeros Ext");
		break;
	case ATA_WRITE_UNCORRECTABLE_EXT:
    {
        uint8_t uncorrectableOption = M_Byte0(features);
        uint32_t numberOfSectors = count;
        char uncorrectableOptionString[31] = { 0 };
        if (numberOfSectors == 0)
        {
            numberOfSectors = 65536;
        }
        switch (uncorrectableOption)
        {
        case WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING://psuedo
            snprntf(uncorrectableOptionString, 30, "Psuedo with logging");
            break;
        case WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING://flagged
            snprntf(uncorrectableOptionString, 30, "Flagged without logging");
            break;
        case WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_5AH://vendor specific
        case WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_A5H://vendor specific
            snprntf(uncorrectableOptionString, 30, "Vendor Specific (%02" PRIX8 "h)", uncorrectableOption);
            break;
        default://reserved/unknown
            snprntf(uncorrectableOptionString, 30, "Unknown Mode (%02" PRIX8 "h)", uncorrectableOption);
            break;
        }
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Uncorrectable Ext - %s  LBA: %" PRIu64 "  Count: %" PRIu32 "", uncorrectableOptionString, lba, numberOfSectors);
    }
		break;
	case ATA_READ_LOG_EXT_DMA:
        get_GPL_Log_Command_Info("Read Log Ext DMA", commandOpCode, features, count, lba, device, commandInfo);
		break;
	case ATA_ZONE_MANAGEMENT_IN:
		//todo: parse feature for what the command is asking to do
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Zone Management In");
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
        snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "NCQ Non-Data");
		break;
	case ATA_SEND_FPDMA:
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Send FPDMA");
		break;
	case ATA_RECEIVE_FPDMA:
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Receive FPDMA");
		break;
	case ATA_SEEK_CMD://TODO: seek can be 7xh....but that also conflicts with new command definitions
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    //case 0x77:
    //case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    //case 0x7C:
    case 0x7D:
    case 0x7E:
    case 0x7F:
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Seek");
		break;
	case ATA_SET_DATE_AND_TIME_EXT://77h
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Date And Time Ext");
		break;
	case ATA_ACCESSABLE_MAX_ADDR://TODO: subcommands //78h
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Accessable Max Address");
		break;
	case ATA_REMOVE_AND_TRUNCATE://7Ch
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Remove And Truncate");
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
		//todo: parse the feature
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Zone Management Out");
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
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Sector Configuration Ext");
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
            uint32_t idleImmdLBA = (lba & 0x00FFFFFFFF) | (M_Nibble0(device) << 24);
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
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Same - LBA: %" PRIu32 " Count: %" PRIu16 "", writeSameLBA, M_Byte0(count));
                }
                else
                {
                    uint16_t cylinder = M_BytesTo2ByteValue(M_Byte2(lba), M_Byte1(lba));
                    uint8_t head = M_Nibble0(device);
                    uint8_t sector = M_Byte0(lba);
                    snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Write Same - Cylinder: %" PRIu16 " Head: %" PRIu8 " Sector: %" PRIu8 " Count: %" PRIu16 "", cylinder, head, sector, M_Byte0(count));
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
	case ATA_SET_FEATURE: //todo: parse feature                               ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		snprintf(commandInfo, ATA_COMMAND_INFO_MAX_LENGTH, "Set Features");
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
            || (commandOpCode >= 0xFA && commandOpCode <= 0xFF)
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

void print_ATA_Comprehensive_SMART_Error_Log(ptrComprehensiveSMARTErrorLog errorLogData, bool errorCommandsOnly /*if this is set, don't show the commands leading up to the error (if available)*/)
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
			uint16_t totalErrorCountLimit = SMART_COMPREHENSIVE_ERRORS_MAX;
			if (errorLogData->extLog)
			{
				totalErrorCountLimit = SMART_EXT_COMPREHENSIVE_ERRORS_MAX;
			}
			for (uint8_t iter = 0; iter < errorLogData->numberOfEntries && iter < totalErrorCountLimit; ++iter)
			{
				printf("\n===============================================\n");
				printf("Error %" PRIu16 " - Drive State: ", iter + 1);
				if (errorLogData->extLog)
				{
					uint8_t errorState = M_Nibble0(errorLogData->smartError->error.state);
					if (errorLogData->extLog)
					{
						errorState = M_Nibble0(errorLogData->extSmartError->error.state);
					}
					switch (errorState)
					{
					case 0:
						printf("Unknown");
						break;
					case 1:
						printf("Sleep");
						break;
					case 2:
						printf("Standby");
						break;
					case 3:
						printf("Active/Idle");
						break;
					case 4:
						printf("Executing Off-line or self test");
						break;
					default:
						if (errorState >= 5 && errorState <= 0x0A)
						{
							printf("Reserved");
						}
						else
						{
							printf("Vendor Specific");
						}
						break;
					}
					printf(" Life Timestamp: ");
					uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
					convert_Seconds_To_Displayable_Time(errorLogData->extSmartError->error.lifeTimestamp * 3600, &years, &days, &hours, &minutes, &seconds);
					print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
					printf("\n");
					uint8_t numberOfCommandsBeforeError = errorLogData->smartError->numberOfCommands;
					if (errorLogData->extLog)
					{
						numberOfCommandsBeforeError = errorLogData->extSmartError->numberOfCommands;
					}
					if (numberOfCommandsBeforeError > 0 && !errorCommandsOnly)
					{
						//TODO: Loop through and print out commands leading up to the error
                        //call get command info function above
					}
					else if (!errorCommandsOnly)
					{
						printf("Commands leading up to the error are not avaiable!\n");
					}
					//TODO: print out the error command! highlight in red? OR some other thing like a -> to make it clear what command was the error?
					printf("Error command:\n");
					if (errorLogData->extLog)
					{
						//ext
                        //call get command info function above
                        //call function to interpret meaning of the error that occured
					}
					else
					{
						//non-ext
                        //call get command info function above
                        //call function to interpret meaning of the error that occured
					}
				}
			}
		}
	}
}