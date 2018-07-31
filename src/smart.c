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

#if !defined (DISABLE_NVME_PASSTHROUGH)

int nvme_Print_Temp_Statistics(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    uint64_t size = 0; 
    uint32_t temperature = 0, pcbTemp = 0, socTemp = 0, scCurrentTemp = 0, scMaxTemp = 0;
    uint64_t maxTemperature = 0, maxSocTemp = 0;
    nvmeGetLogPageCmdOpts   cmdOpts;
    nvmeSmartLog            smartLog;
    nvmeSuperCapDramSmart   scDramSmart;

    if (is_Seagate(device, false))
    {
        //STEP-1 : Get Current Temperature from SMART

        memset(&smartLog, 0, sizeof(nvmeSmartLog));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = (uint64_t)(&smartLog);
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
            if (VERBOSITY_QUIET < g_verbosity)
            {
                printf("Error: Could not retrieve Log Page 0x02\n");
            }            
        }

        // STEP-2 : Get Max temperature form Ext SMART-id 194
        // This I will add after pulling Linga's changes


        // STEP-3 : Get Max temperature form SuperCap DRAM temperature
        memset(&scDramSmart, 0, sizeof(nvmeSuperCapDramSmart));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = (uint64_t)(&scDramSmart);
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
            if (VERBOSITY_QUIET < g_verbosity)
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
    uint64_t size = 0; 
    uint32_t correctPcieEc = 0, uncorrectPcieEc = 0;
    nvmeGetLogPageCmdOpts   cmdOpts;
    nvmePcieErrorLogPage    pcieErrorLog;

    if (is_Seagate(device, false))
    {

        memset(&pcieErrorLog, 0, sizeof(nvmePcieErrorLogPage));

        cmdOpts.nsid = NVME_ALL_NAMESPACES;
        cmdOpts.addr = (uint64_t)(&pcieErrorLog);
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
            if (VERBOSITY_QUIET < g_verbosity)
            {
                printf("Error: Could not retrieve Log Page 0x02\n");
            }            
        }
    }

    return ret;
}

#endif

