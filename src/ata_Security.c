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

// \file ata_Security.c
// \brief This file defines the function calls for performing some ATA Security operations

#include "operations_Common.h"
#include "ata_Security.h"

bool sat_ATA_Security_Protocol_Supported(tDevice *device)
{
    bool supported = false;
    //For non-ATA/IDE interfaces, we need to check if the translator (SATL) supports the ATA security protocol.
    if (device->drive_info.interface_type != IDE_INTERFACE)
    {
        uint8_t securityBuf[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, LEGACY_DRIVE_SEC_SIZE, securityBuf))
        {
            uint16_t length = M_BytesTo2ByteValue(securityBuf[6], securityBuf[7]);
            uint16_t bufIter = 8;
			for (; (bufIter - 8) < length && bufIter < LEGACY_DRIVE_SEC_SIZE; bufIter++)
            {
                switch (securityBuf[bufIter])
                {
                case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
                {
                    //the supported list shows this protocol, but try reading the page too...if that fails then we know it's only a partial implementation.
                    uint8_t ataSecurityInfo[16] = { 0 };
                    if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0, false, 16, ataSecurityInfo))
                    {
						if (ataSecurityInfo[1] == 0x0E)//Checking that the length matches to make sure we got a good response
						{
							supported = true;
						}
                    }
                }
                break;
                default:
                    break;
                }
            }
        }
    }
    return supported;
}
//TODO: we may want to revisit this function since there are up to 3 places to get the information we need between SAT translation, ID, and IDData log
void get_ATA_Security_Info(tDevice *device, ptrATASecurityStatus securityStatus, bool useSAT)
{
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        uint8_t ataSecurityInfo[16] = { 0 };
        if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0, false, 16, ataSecurityInfo))
        {
            securityStatus->securityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[2], ataSecurityInfo[3]) * 2;
            if (securityStatus->securityEraseUnitTimeMinutes == (32767 * 2))
            {
                securityStatus->securityEraseUnitTimeMinutes = UINT16_MAX;
            }
            securityStatus->enhancedSecurityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[4], ataSecurityInfo[5]) * 2;
            if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == (32767 * 2))
            {
                securityStatus->enhancedSecurityEraseUnitTimeMinutes = UINT16_MAX;
            }
            securityStatus->masterPasswordIdentifier = M_BytesTo2ByteValue(ataSecurityInfo[6], ataSecurityInfo[7]);
            if (ataSecurityInfo[8] & BIT0)
            {
                securityStatus->masterPasswordCapability = true;
            }
            if (ataSecurityInfo[9] & BIT0)
            {
                securityStatus->securitySupported = true;
            }
            if (ataSecurityInfo[9] & BIT1)
            {
                securityStatus->securityEnabled = true;
            }
            if (ataSecurityInfo[9] & BIT2)
            {
                securityStatus->securityLocked = true;
            }
            if (ataSecurityInfo[9] & BIT3)
            {
                securityStatus->securityFrozen = true;
            }
            if (ataSecurityInfo[9] & BIT4)
            {
                securityStatus->securityCountExpired = true;
            }
            if (ataSecurityInfo[9] & BIT5)
            {
                securityStatus->enhancedEraseSupported = true;
            }
        }
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //word 128
        if (device->drive_info.IdentifyData.ata.Word128 != 0 && device->drive_info.IdentifyData.ata.Word128 != UINT16_MAX && device->drive_info.IdentifyData.ata.Word128 & BIT0)
        {
            securityStatus->securitySupported = true;
            if (device->drive_info.IdentifyData.ata.Word128 & BIT1)
            {
                securityStatus->securityEnabled = true;
            }
            if (device->drive_info.IdentifyData.ata.Word128 & BIT2)
            {
                securityStatus->securityLocked = true;
            }
            if (device->drive_info.IdentifyData.ata.Word128 & BIT3)
            {
                securityStatus->securityFrozen = true;
            }
            if (device->drive_info.IdentifyData.ata.Word128 & BIT4)
            {
                securityStatus->securityCountExpired = true;
            }
            if (device->drive_info.IdentifyData.ata.Word128 & BIT5)
            {
                securityStatus->enhancedEraseSupported = true;
            }
            if (device->drive_info.IdentifyData.ata.Word128 & BIT8)
            {
                securityStatus->masterPasswordCapability = true;
            }
            //word 89
            if (device->drive_info.IdentifyData.ata.Word089 & BIT15)
            {
                securityStatus->extendedTimeFormat = true;
                //bits 14:0
                securityStatus->securityEraseUnitTimeMinutes = (device->drive_info.IdentifyData.ata.Word089 & 0x7FFF) * 2;
                if (securityStatus->securityEraseUnitTimeMinutes == (32767 * 2))
                {
                    securityStatus->securityEraseUnitTimeMinutes = UINT16_MAX;
                }
            }
            else
            {
                //bits 7:0
                securityStatus->securityEraseUnitTimeMinutes = M_Byte0(device->drive_info.IdentifyData.ata.Word089) * 2;
                if (securityStatus->securityEraseUnitTimeMinutes == (255 * 2))
                {
                    securityStatus->securityEraseUnitTimeMinutes = UINT16_MAX;
                }
            }
            //word 90
            if (device->drive_info.IdentifyData.ata.Word090 & BIT15)
            {
                securityStatus->extendedTimeFormat = true;
                //bits 14:0
                securityStatus->enhancedSecurityEraseUnitTimeMinutes = (device->drive_info.IdentifyData.ata.Word090 & 0x7FFF) * 2;
                if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == (32767 * 2))
                {
                    securityStatus->enhancedSecurityEraseUnitTimeMinutes = UINT16_MAX;
                }
            }
            else
            {
                //bits 7:0
                securityStatus->enhancedSecurityEraseUnitTimeMinutes = M_Byte0(device->drive_info.IdentifyData.ata.Word090) * 2;
                if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == (255 * 2))
                {
                    securityStatus->enhancedSecurityEraseUnitTimeMinutes = UINT16_MAX;
                }
            }
            //word 92
            securityStatus->masterPasswordIdentifier = device->drive_info.IdentifyData.ata.Word092;
        }
        if (device->drive_info.IdentifyData.ata.Word069 != 0 && device->drive_info.IdentifyData.ata.Word069 != UINT16_MAX)
        {
            securityStatus->encryptAll = device->drive_info.IdentifyData.ata.Word069 & BIT4;
        }
    }
    //read ID data log page for security bits to get restrictedSanitizeOverridesSecurity bit
    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        uint8_t securityPage[512] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, 0, 0, securityPage, 512, 0))
        {
            if (M_BytesTo2ByteValue(securityPage[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2) + 1], securityPage[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2)]) * 512 > 0)
            {
                memset(&securityPage, 0, 512);
                //IDData log suppored. Read first page to see if security subpage (06h) is supported
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_PAGES, securityPage, 512, 0))
                {
                    uint8_t pageNumber = securityPage[2];
                    uint16_t revision = M_BytesTo2ByteValue(securityPage[1], securityPage[0]);
                    if (pageNumber == (uint8_t)ATA_ID_DATA_LOG_SUPPORTED_PAGES && revision >= 0x0001)
                    {
                        uint8_t listLen = securityPage[8];
                        for (uint8_t iter = 9; iter < (listLen + 8) && iter < 512; ++iter)
                        {
                            bool foundSecurityPage = false;
                            switch (securityPage[iter])
                            {
                            case ATA_ID_DATA_LOG_SECURITY:
                                foundSecurityPage = true;
                                memset(securityPage, 0, 512);
                                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SECURITY, securityPage, 512, 0))
                                {
                                    //make sure we got the right page first!
                                    uint64_t header = M_BytesTo8ByteValue(securityPage[7], securityPage[6],securityPage[5],securityPage[4],securityPage[3],securityPage[2],securityPage[1],securityPage[0]);
                                    if (header & BIT63 && M_Word0(header) >= 0x0001  && M_Byte2(header) == ATA_ID_DATA_LOG_SECURITY)
                                    {
                                        uint64_t securityCapabilities = M_BytesTo8ByteValue(securityPage[55], securityPage[54],securityPage[53],securityPage[52],securityPage[51],securityPage[50],securityPage[49],securityPage[48]);
                                        if (securityCapabilities & BIT63)
                                        {
                                            securityStatus->restrictedSanitizeOverridesSecurity = securityCapabilities & BIT7;
                                            securityStatus->encryptAll = securityCapabilities & BIT0;
                                        }
                                    }
                                }
                                break;
                            default:
                                break;
                            }
                            if (foundSecurityPage)
                            {
                                //exit the loop since we got what we wanted.
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    //set security state
    if (securityStatus->securityEnabled == false && securityStatus->securityLocked == false && securityStatus->securityFrozen == false)
    {
        securityStatus->securityState = ATA_SEC1;
    }
    else if (securityStatus->securityEnabled == false && securityStatus->securityLocked == false && securityStatus->securityFrozen == true)
    {
        securityStatus->securityState = ATA_SEC2;
    }
    else if (securityStatus->securityEnabled == true && securityStatus->securityLocked == true && securityStatus->securityFrozen == false)
    {
        securityStatus->securityState = ATA_SEC4;
    }
    else if (securityStatus->securityEnabled == true && securityStatus->securityLocked == false && securityStatus->securityFrozen == false)
    {
        securityStatus->securityState = ATA_SEC5;
    }
    else if (securityStatus->securityEnabled == true && securityStatus->securityLocked == false && securityStatus->securityFrozen == true)
    {
        securityStatus->securityState = ATA_SEC6;
    }
}

void print_ATA_Security_Info(ptrATASecurityStatus securityStatus, bool satSecurityProtocolSupported)
{
    printf("\n====ATA Security Information====\n");
    if (securityStatus->securitySupported)
    {
        printf("Security State: %u\n", securityStatus->securityState);
        //Now print out the other bits
        printf("\tEnabled: ");
        if (securityStatus->securityEnabled)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\tLocked: ");
        if (securityStatus->securityLocked)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\tFrozen: ");
        if (securityStatus->securityFrozen)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\tPassword Attempts Exceeded: ");
        if (securityStatus->securityCountExpired)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        //Show master password capability and identifier
        printf("Master Password Capability: ");
        if (securityStatus->masterPasswordCapability)
        {
            printf("Maximum\n");
        }
        else
        {
            printf("High\n");
        }
        printf("Master Password Identifier: ");
        if (securityStatus->masterPasswordIdentifier != 0x0000 && securityStatus->masterPasswordIdentifier != UINT16_MAX)
        {
            printf("%" PRIu16, securityStatus->masterPasswordIdentifier);
            if (securityStatus->masterPasswordIdentifier == 0xFFFE)
            {
                //possibly the original used at manufacture
                printf(" (may be set to manufacture master password)");
            }
            printf("\n");
        }
        else
        {
            printf("Not supported\n");
        }
        //Now print out security erase times
        printf("Enhanced Erase Time Estimate: ");
        if (securityStatus->enhancedEraseSupported)
        {
            if (securityStatus->extendedTimeFormat)
            {
                if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == 0)
                {
                    printf("Not reported\n");
                }
                else if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == UINT16_MAX)
                {
                    uint64_t totalSeconds = 65532 * 60;
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    printf(">");
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
                else
                {
                    uint64_t totalSeconds = securityStatus->enhancedSecurityEraseUnitTimeMinutes * 60;
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
            }
            else
            {
                if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == 0)
                {
                    printf("Not reported\n");
                }
                else if (securityStatus->enhancedSecurityEraseUnitTimeMinutes == UINT16_MAX)
                {
                    uint64_t totalSeconds = 508 * 60;
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    printf(">");
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
                else
                {
                    uint64_t totalSeconds = securityStatus->enhancedSecurityEraseUnitTimeMinutes * 60;
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
            }
        }
        else
        {
            printf("Not Supported\n");
        }
        printf("Security Erase Time Estimate: ");
        if (securityStatus->extendedTimeFormat)
        {
            if (securityStatus->securityEraseUnitTimeMinutes == 0)
            {
                printf("Not reported\n");
            }
            else if (securityStatus->securityEraseUnitTimeMinutes == UINT16_MAX)
            {
                uint64_t totalSeconds = 65532 * 60;
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                printf(">");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
            else
            {
                uint64_t totalSeconds = securityStatus->securityEraseUnitTimeMinutes * 60;
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
        }
        else
        {
            if (securityStatus->securityEraseUnitTimeMinutes == 0)
            {
                printf("Not reported\n");
            }
            else if (securityStatus->securityEraseUnitTimeMinutes == UINT16_MAX)
            {
                uint64_t totalSeconds = 508 * 60;
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                printf(">");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
            else
            {
                uint64_t totalSeconds = securityStatus->securityEraseUnitTimeMinutes * 60;
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
        }
        printf("All user data is encrypted: ");
        if (securityStatus->encryptAll)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("Restricted Sanitize Overrides ATA Security: ");
        if (securityStatus->restrictedSanitizeOverridesSecurity)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("SAT security protocol supported: ");
        if (satSecurityProtocolSupported)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else
    {
        printf("ATA Security is not supported on this device.\n");
    }
}

void set_ATA_Security_Password_In_Buffer(uint8_t *ptrData, const char *ATAPassword, eATASecurityPasswordType passwordType, eATASecurityMasterPasswordCapability masterPasswordCapability, uint16_t masterPasswordIdentifier)
{
    if (ptrData)
    {
        uint16_t *wordPtr = (uint16_t*)&ptrData[0];
        //copy the password in, but the max length is 32 bytes according to the spec!
        memcpy(&wordPtr[1], ATAPassword, M_Min(strlen(ATAPassword), 32));
        if (passwordType == ATA_PASSWORD_MASTER)
        {
            wordPtr[0] |= BIT0;
            if (masterPasswordCapability == ATA_MASTER_PASSWORD_MAXIMUM)
            {
                wordPtr[0] |= BIT8;
            }
            wordPtr[17] = masterPasswordIdentifier;
        }
    }
}

void set_ATA_Security_Erase_Type_In_Buffer(uint8_t *ptrData, eATASecurityEraseType eraseType)
{
    if (ptrData)
    {
        if (eraseType == ATA_SECURITY_ERASE_ENHANCED_ERASE)
        {
            uint16_t *wordPtr = (uint16_t*)&ptrData[0];
            wordPtr[0] |= BIT1;
        }
    }
}


int set_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool master, bool masterPasswordCapabilityMaximum, uint16_t masterPasswordIdentifier, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * sizeof(uint8_t), sizeof(uint8_t));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    eATASecurityPasswordType passwordType = ATA_PASSWORD_USER;
    eATASecurityMasterPasswordCapability masterCapability = ATA_MASTER_PASSWORD_HIGH;
    if (master)
    {
        passwordType = ATA_PASSWORD_MASTER;
        if (masterPasswordCapabilityMaximum)
        {
            masterCapability = ATA_MASTER_PASSWORD_MAXIMUM;
        }
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, ATAPassword, passwordType, masterCapability, masterPasswordIdentifier);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0x0001, false, 36, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Set_Password(device, securityPassword);
    }
    safe_Free(securityPassword);
    return ret;
}

int disable_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool master, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * sizeof(uint8_t), sizeof(uint8_t));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    eATASecurityPasswordType passwordType = ATA_PASSWORD_USER;
    if (master)
    {
        passwordType = ATA_PASSWORD_MASTER;
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, ATAPassword, passwordType, ATA_MASTER_PASSWORD_HIGH, 0);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0x0006, false, 36, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Disable_Password(device, securityPassword);
    }
    safe_Free(securityPassword);
    return ret;
}

int unlock_ATA_Security(tDevice *device, const char *ATAPassword, bool master, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * sizeof(uint8_t), sizeof(uint8_t));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    eATASecurityPasswordType passwordType = ATA_PASSWORD_USER;
    if (master)
    {
        passwordType = ATA_PASSWORD_MASTER;
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, ATAPassword, passwordType, ATA_MASTER_PASSWORD_HIGH, 0);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0x0002, false, 36, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Unlock(device, securityPassword);
    }
    safe_Free(securityPassword);
    return ret;
}

int start_ATA_Security_Erase(tDevice *device, const char *ATAPassword, bool master, bool enhanced, uint32_t timeout, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityErase = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * sizeof(uint8_t), sizeof(uint8_t));
    if (!securityErase)
    {
        return MEMORY_FAILURE;
    }
    eATASecurityPasswordType passwordType = ATA_PASSWORD_USER;
    if (master)
    {
        passwordType = ATA_PASSWORD_MASTER;
    }
    set_ATA_Security_Password_In_Buffer(securityErase, ATAPassword, passwordType, ATA_MASTER_PASSWORD_HIGH, 0);
    eATASecurityEraseType eraseType = ATA_SECURITY_ERASE_STANDARD_ERASE;
    if (enhanced)
    {
        eraseType = ATA_SECURITY_ERASE_ENHANCED_ERASE;
    }
    set_ATA_Security_Erase_Type_In_Buffer(securityErase, eraseType);
    //first send the erase prepare command
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0x0003, false, 0, NULL, 15);
    }
    else
    {
        ret = ata_Security_Erase_Prepare(device);
    }
    if (SUCCESS == ret)
    {
        //now send the erase command
        if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
        {
            ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0x0004, false, 36, securityErase, timeout);
        }
        else
        {
            ret = ata_Security_Erase_Unit(device, securityErase, timeout);
        }
    }
    safe_Free(securityErase);
    return ret;
}

int run_Disable_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool userMaster)
{
    int ret = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (device->drive_info.drive_type == ATA_DRIVE || satATASecuritySupported)
    {
        ataSecurityStatus securityStatus;
        memset(&securityStatus, 0, sizeof(ataSecurityStatus));
        get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
        if (securityStatus.securitySupported)
        {
            if (securityStatus.securityEnabled)
            {
                //if frozen, then we can't do anything
                if (securityStatus.securityFrozen)
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Security is Frozen. Cannot disable password.\n");
                    }
                    ret = FROZEN;
                }
                else
                {
                    if (securityStatus.securityLocked)
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Attempting to unlock security with password = \"%s\".\n", ATAPassword);
                        }
                        if (SUCCESS == unlock_ATA_Security(device, ATAPassword, userMaster, satATASecuritySupported))
                        {
                            securityStatus.securityLocked = false;
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Unable to unlock drive with password = \"%s\".\n", ATAPassword);
                            }
                        }
                    }
                    //now check security locked again because the above if statement should change it if the unlock was successful
                    if (!securityStatus.securityLocked)
                    {
                        ret = disable_ATA_Security_Password(device, ATAPassword, userMaster, satATASecuritySupported);
                    }
                    else
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Security is Locked. Cannot disable password.\n");
                        }
                        ret = FAILURE;
                    }
                }
            }
            else
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Security Feature is not enabled. Nothing to do.\n");
                }
                ret = SUCCESS;
            }
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Security Feature Not Supported by device.\n");
            }
            ret = NOT_SUPPORTED;
        }
    }
    else //this is ATA specific and there's nothing to do on other drives since they don't support this
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int run_ATA_Security_Erase(tDevice *device, bool enhanced, bool master, const char *password, bool pollForProgress)
{
    int result = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (device->drive_info.drive_type != ATA_DRIVE && !satATASecuritySupported)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("ATA Security Erase not supported on this drive\n");
        }
        return NOT_SUPPORTED;
    }
    else
    {        
        uint16_t eraseTimeMinutes = 0;
        ataSecurityStatus securityStatus;
        memset(&securityStatus, 0, sizeof(ataSecurityStatus));
        get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
        if (securityStatus.securitySupported)
        {
            //if they asked for enhanced erase, make sure it is supported
            if (!securityStatus.enhancedEraseSupported && enhanced)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Enhanced ATA security erase is not supported on this drive.\n");
                }
                return NOT_SUPPORTED;
            }
            //check if the drive is frozen
            if (securityStatus.securityFrozen)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("ATA security is frozen.\n");
                }
                return FROZEN;
            }
            //get the erase time for the requested erase
            if (enhanced)
            {
                eraseTimeMinutes = securityStatus.enhancedSecurityEraseUnitTimeMinutes;
            }
            else
            {
                eraseTimeMinutes = securityStatus.securityEraseUnitTimeMinutes;
            }
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("ATA security not supported.\n");
            }
            return NOT_SUPPORTED;
        }
        if (securityStatus.securityLocked)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Attempting to unlock security with password = \"%s\".\n", password);
            }
            if (SUCCESS == unlock_ATA_Security(device, password, master, satATASecuritySupported))
            {
                securityStatus.securityLocked = false;
            }
            else
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Unable to unlock drive with password = \"%s\".\n", password);
                }
                return FAILURE;
            }
        }
        if (!securityStatus.securityEnabled)
        {
            //set the password
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Setting ATA Security password to \"%s\"\n", password);
            }
            if (SUCCESS != set_ATA_Security_Password(device, password, master, false, 0, satATASecuritySupported))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Failed to set ATA Security Password. Cannot erase drive.\n");
                }
                return FAILURE;
            }
        }

        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Starting ");
            if (enhanced)
            {
                printf("Enhanced ATA Security Erase using ");
            }
            else
            {
                printf("ATA Security Erase using ");
            }
            if (master)
            {
                printf("Master password: ");
            }
            else
            {
                printf("User password: ");
            }
            printf("\"%s\"\n", password);
            if (eraseTimeMinutes == UINT16_MAX || eraseTimeMinutes == 0)
            {
                if (eraseTimeMinutes == 0)
                {
                    printf("\n\tThe drive did not report an erase time estimate.\n");
                }
                else
                {
                    printf("\n\tThe drive reported an estimated erase time longer than\n");
                    if (securityStatus.extendedTimeFormat)
                    {
                        printf("\t65532 minutes (max per ATA specification).\n");
                    }
                    else
                    {
                        printf("\t508 minutes (max per ATA specification).\n");
                    }
                }
                printf("\tA completion estimate is not available for this drive.\n");
            }
            else
            {
                time_t currentTime = time(NULL);
                time_t futureTime = get_Future_Date_And_Time(currentTime, eraseTimeMinutes * 60);
                uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(eraseTimeMinutes * 60, NULL, &days, &hours, &minutes, &seconds);
                printf("\n\tCurrent Time: %s\tDrive reported completion time: ", ctime((const time_t*)&currentTime));
                print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
                printf("from now.\n");
                printf("\tEstimated completion Time : %s", ctime((const time_t *)&futureTime));
            }
            printf("\n\tPlease DO NOT remove power to the drive during the erase\n");
            printf("\tas this will leave it in an uninitialized state with the password set.\n");
            printf("\tIf the power is removed, rerun this test with your utility.\n");
            printf("\tUpon erase completion, the password is automatically cleared.\n\n");
        }
        seatimer_t ataSecureEraseTimer;
        memset(&ataSecureEraseTimer, 0, sizeof(seatimer_t));
        start_Timer(&ataSecureEraseTimer);
        int ataEraseResult = start_ATA_Security_Erase(device, password, master, enhanced, UINT32_MAX, satATASecuritySupported);
        stop_Timer(&ataSecureEraseTimer);
        //before we read the bitfield again...try requesting sense data to see if that says there was a reset on the bus. (6h/29h/00h)
        bool hostResetDuringErase = false;
        if (!satATASecuritySupported) //Only do the code below if we aren't using the SAT security protocol to perform the erase.
        {
            //If the host issued a reset, we may not get back correct status, but the status after the reset.
            //After talking to a firmware engineer, a status of 50h and error of 01h would be what we could see.
            //I added this check as a test to set the "host reset the drive" message at the end of the operation. - TJE
            if (device->drive_info.lastCommandRTFRs.status == 0x50 && device->drive_info.lastCommandRTFRs.error == 0x01)
            {
                //ataEraseResult = FAILURE; //This is commented out because I'm not sure this is the right place to do this...
                //The code that checks the security bits should still catch a failure. - TJE
                hostResetDuringErase = true;
            }
        }
#if defined (_WIN32) || defined(__FreeBSD__)
        if (device->drive_info.interface_type != IDE_INTERFACE)
#endif
        {
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_UNIT_ATTENTION && asc == 0x29 && ascq == 0)
            {
                hostResetDuringErase = true;
            }
            uint8_t validateCompletion[SPC3_SENSE_LEN] = { 0 };
            scsi_Request_Sense_Cmd(device, false, validateCompletion, SPC3_SENSE_LEN);
            get_Sense_Key_ASC_ASCQ_FRU(validateCompletion, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (device->deviceVerbosity >= VERBOSITY_BUFFERS)
            {
                printf("ATA Security Validate Erase Completion, validate completion buffer:\n");
                print_Data_Buffer(validateCompletion, SPC3_SENSE_LEN, false);
            }
            int senseResult = check_Sense_Key_ASC_ASCQ_And_FRU(device, senseKey, asc, ascq, fru);
            if (senseResult != SUCCESS && ataEraseResult == SUCCESS)
            {
                ataEraseResult = FAILURE;
            }
            if (senseKey == SENSE_KEY_UNIT_ATTENTION && asc == 0x29 && ascq == 0)
            {
                hostResetDuringErase = true;
            }
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (device->deviceVerbosity >= VERBOSITY_BUFFERS)
            {
                printf("ATA Security Validate Erase Completion, request sense command completion:\n");
                print_Data_Buffer(validateCompletion, SPC3_SENSE_LEN, false);
            }
            if (senseKey == SENSE_KEY_UNIT_ATTENTION && asc == 0x29 && ascq == 0)
            {
                hostResetDuringErase = true;
            }
        }
        //Read the bitfield again to check if the drive is in a good or bad security state...a success should only leave it at supported. If locked or enabled, we need to fail and disable the password.
        if (satATASecuritySupported)
        {
            //force an identify information update to make sure the security protocol information is not being cached by the controller. VPD 89h will force the controller to issue a new identify command and it SHOULD also update anything the controller is caching.
            uint8_t ataVPDPage[VPD_ATA_INFORMATION_LEN] = { 0 };
            if (SUCCESS == scsi_Inquiry(device, ataVPDPage, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
            {
                memcpy((uint8_t*)&device->drive_info.IdentifyData.ata.Word000, &ataVPDPage[60], LEGACY_DRIVE_SEC_SIZE);
            }
        }
        else
        {
            //issue an identify device command before we read the ATA security bits to make sure the data isn't stale in our structure.
            ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
        }
        memset(&securityStatus, 0, sizeof(ataSecurityStatus));
        get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
        if (SUCCESS == ataEraseResult && !securityStatus.securityEnabled && !securityStatus.securityLocked)
        {
            
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\tATA security erase has completed successfully.\n");
                printf("\tTime to erase was ");
                    
            }
            result = SUCCESS;
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\tATA Security erase failed to complete after ");
            }
            result = FAILURE;
        }
        
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
            convert_Seconds_To_Displayable_Time((uint64_t)get_Seconds(ataSecureEraseTimer), &years, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
            printf("\n\n");
        }
        if (result == FAILURE || hostResetDuringErase)
        {
            //disable the password if it's enabled
            if (securityStatus.securityLocked)
            {
                unlock_ATA_Security(device, password, master, satATASecuritySupported);
                memset(&securityStatus, 0, sizeof(ataSecurityStatus));
                get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
            }
            if (securityStatus.securityEnabled && !securityStatus.securityLocked)
            {
                if (SUCCESS == disable_ATA_Security_Password(device, password, master, satATASecuritySupported))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("\tThe ATA Security password used during erase has been cleared.\n\n");
                    }
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("\tUnable to remove the ATA security password.\n\n");
                    }
                }
            }
            else if(VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\tThe drive is in a security state where clearing the password is not possible.\n\n");
            }
            if (hostResetDuringErase)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\tThe host reset the drive during the erase.\n\tEnsure no other applications are trying to access\n\tthe drive while it is erasing.\n\n");
                }
            }
        }
    }
    return result;
}
