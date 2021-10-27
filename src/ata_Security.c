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

// \file ata_Security.c
// \brief This file defines the function calls for performing some ATA Security operations

#include "operations_Common.h"
#include "ata_Security.h"
#include <ctype.h>
#include "platform_helper.h"

bool sat_ATA_Security_Protocol_Supported(tDevice *device)
{
    bool supported = false;
    //For non-ATA/IDE interfaces, we need to check if the translator (SATL) supports the ATA security protocol.
    //TODO: we may not need this check since software SAT in the opensea-transport library can do this translation
    if (device->drive_info.interface_type != IDE_INTERFACE)
    {
        uint8_t securityBuf[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, LEGACY_DRIVE_SEC_SIZE, securityBuf))
        {
            uint16_t length = M_BytesTo2ByteValue(securityBuf[6], securityBuf[7]);
            uint16_t bufIter = 8;
            for (; (bufIter - 8) < length && bufIter < LEGACY_DRIVE_SEC_SIZE; ++bufIter)
            {
                switch (securityBuf[bufIter])
                {
                case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
                {
                    //the supported list shows this protocol, but try reading the page too...if that fails then we know it's only a partial implementation.
                    uint8_t ataSecurityInfo[SAT_SECURITY_INFO_LEN] = { 0 };
                    if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_READ_INFO, false, SAT_SECURITY_INFO_LEN, ataSecurityInfo))
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
        if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_READ_INFO, false, SAT_SECURITY_INFO_LEN, ataSecurityInfo))
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
                    if (pageNumber == C_CAST(uint8_t, ATA_ID_DATA_LOG_SUPPORTED_PAGES) && revision >= 0x0001)
                    {
                        uint8_t listLen = securityPage[8];
                        for (uint16_t iter = 9; iter < C_CAST(uint16_t, listLen + 8) && iter < UINT16_C(512); ++iter)
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
                    uint64_t totalSeconds = UINT64_C(65532) * UINT64_C(60);
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    printf(">");
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
                else
                {
                    uint64_t totalSeconds = C_CAST(uint64_t, securityStatus->enhancedSecurityEraseUnitTimeMinutes) * UINT64_C(60);
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
                    uint64_t totalSeconds = UINT64_C(508) * UINT64_C(60);
                    uint8_t days = 0, hours = 0, minutes = 0;
                    convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                    printf(">");
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                    printf("\n");
                }
                else
                {
                    uint64_t totalSeconds = C_CAST(uint64_t, securityStatus->enhancedSecurityEraseUnitTimeMinutes) * UINT64_C(60);
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
                uint64_t totalSeconds = UINT64_C(65532) * UINT64_C(60);
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                printf(">");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
            else
            {
                uint64_t totalSeconds = C_CAST(uint64_t, securityStatus->securityEraseUnitTimeMinutes) * UINT64_C(60);
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
                uint64_t totalSeconds = UINT64_C(508) * UINT64_C(60);
                uint8_t days = 0, hours = 0, minutes = 0;
                convert_Seconds_To_Displayable_Time(totalSeconds, NULL, &days, &hours, &minutes, NULL);
                printf(">");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, NULL);
                printf("\n");
            }
            else
            {
                uint64_t totalSeconds = C_CAST(uint64_t, securityStatus->securityEraseUnitTimeMinutes) * UINT64_C(60);
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

void print_ATA_Security_Password(ptrATASecurityPassword ataPassword)
{
    if (ataPassword)
    {
        //first check if it is empty
        if (ataPassword->passwordLength > 0)
        {
            //now we need to check if it's a printable string or not and print it between quotes if it is, otherwise as a sequence of hex bytes
            bool isASCIIString = true;//assume it is a string since it will most likely be one.
            for(uint8_t iter = 0; iter < ataPassword->passwordLength && iter < ATA_SECURITY_MAX_PW_LENGTH; ++iter)
            {
                if (iscntrl(ataPassword->password[iter]) || ataPassword->password[iter] & BIT7)//this function is the opposite of isPrint and should work for what we want. We also want to check if bit 7 is set
                {
                    isASCIIString = false;
                    break;
                }
            }
            if (isASCIIString)
            {
                //printf between quotes
                printf(" \"%s\"", ataPassword->password);
            }
            else
            {
                //need to show as hex bytes
                for(uint8_t iter = 0; iter < ataPassword->passwordLength && iter < ATA_SECURITY_MAX_PW_LENGTH; ++iter)
                {
                    printf(" %02" PRIX8 "h", ataPassword->password[iter]);
                    if (iter + 1 < ataPassword->passwordLength)
                    {
                        printf(", ");
                    }
                }
            }
        }
        else
        {
            printf(" (password is empty)");
        }
        if (ataPassword->passwordType == ATA_PASSWORD_MASTER)
        {
            printf(" (Master)\n");
        }
        else
        {
            printf(" (User)\n");
        }
    }
}

void set_ATA_Security_Password_In_Buffer(uint8_t *ptrData, ptrATASecurityPassword ataPassword, bool setPassword, bool eraseUnit)
{
    if (ptrData && ataPassword)
    {
        //copy the password in, but the max length is 32 bytes according to the spec!
        memcpy(&ptrData[2], ataPassword->password, M_Min(ataPassword->passwordLength, ATA_SECURITY_MAX_PW_LENGTH));
        if (setPassword)//if setting the password in the set password command, we need to set a few other things up
        {
            //set master password capability
            if (ataPassword->masterCapability == ATA_MASTER_PASSWORD_MAXIMUM)
            {
                ptrData[1] |= BIT0;//word zero bit 8
            }
        }
        else if (eraseUnit)
        {
            if (ataPassword->zacSecurityOption == ATA_ZAC_ERASE_FULL_ZONES)
            {
                ptrData[0] |= BIT2;//word zero bit 2
            }
        }
        if (ataPassword->passwordType == ATA_PASSWORD_MASTER)
        {
            ptrData[0] |= BIT0;//Word 0, bit 0 for the identifier bit to say it's the master password
            if (setPassword)//if setting the password in the set password command, we need to set a few other things up
            {
                //set the master password identifier.
                //Since this is ATA, this is little endian format. 
                //TODO: verify this is set correctly on big endian
                //Word 17
                ptrData[34] = M_Byte1(ataPassword->masterPWIdentifier);
                ptrData[35] = M_Byte0(ataPassword->masterPWIdentifier);
            }
        }
    }
}

void set_ATA_Security_Erase_Type_In_Buffer(uint8_t *ptrData, eATASecurityEraseType eraseType)
{
    if (ptrData)
    {
        if (eraseType == ATA_SECURITY_ERASE_ENHANCED_ERASE)
        {
            //Word zero, bit 1
            //NOTE: SAT spec has this incorrect in erase unit parameter list unless the SATL actually changes the incomming buffer before issuing the command (which seems like more work than it would actually do)
            ptrData[1] |= BIT1;
        }
    }
}


int set_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, &ataPassword, true, false);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_SET_PASSWORD, false, SAT_SECURITY_PASS_LEN, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Set_Password(device, securityPassword);
    }
    safe_Free_aligned(securityPassword)
    return ret;
}

int disable_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, &ataPassword, false, false);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_DISABLE_PASSWORD, false, SAT_SECURITY_PASS_LEN, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Disable_Password(device, securityPassword);
    }
    safe_Free_aligned(securityPassword)
    return ret;
}

int unlock_ATA_Security(tDevice *device, ataSecurityPassword ataPassword, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityPassword = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!securityPassword)
    {
        return MEMORY_FAILURE;
    }
    set_ATA_Security_Password_In_Buffer(securityPassword, &ataPassword, false, false);
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_UNLOCK, false, SAT_SECURITY_PASS_LEN, securityPassword, 15);
    }
    else
    {
        ret = ata_Security_Unlock(device, securityPassword);
    }
    safe_Free_aligned(securityPassword)
    return ret;
}

int start_ATA_Security_Erase(tDevice *device, ataSecurityPassword ataPassword, eATASecurityEraseType eraseType, uint32_t timeout, bool useSAT)
{
    int ret = SUCCESS;
    uint8_t *securityErase = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!securityErase)
    {
        return MEMORY_FAILURE;
    }
    set_ATA_Security_Password_In_Buffer(securityErase, &ataPassword, false, true);
    set_ATA_Security_Erase_Type_In_Buffer(securityErase, eraseType);
    //first send the erase prepare command
    if (useSAT)//if SAT ATA security supported, use it so the SATL manages the erase.
    {
        ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_ERASE_PREPARE, false, 0, NULL, 15);
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
            ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_ERASE_UNIT, false, SAT_SECURITY_PASS_LEN, securityErase, timeout);
        }
        else
        {
            ret = ata_Security_Erase_Unit(device, securityErase, timeout);
        }
    }
    safe_Free_aligned(securityErase)
    return ret;
}

//Attempts an unlock if needed
//TODO: Check if security count expired!
int run_Disable_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
{
    int ret = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (forceSATvalid)
    {
        satATASecuritySupported = forceSAT;
    }
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
                        if (securityStatus.securityCountExpired)
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Password attempts exceeded. You must power cycle the drive to clear the attempt counter and retry the operation.\n");
                            }
                            return FAILURE;
                        }
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Attempting to unlock security with password = ");
                            print_ATA_Security_Password(&ataPassword);
                        }
                        if (SUCCESS == unlock_ATA_Security(device, ataPassword, satATASecuritySupported))
                        {
                            securityStatus.securityLocked = false;
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Unable to unlock drive with password = ");
                                print_ATA_Security_Password(&ataPassword);
                            }
                        }
                    }
                    //now check security locked again because the above if statement should change it if the unlock was successful
                    if (!securityStatus.securityLocked)
                    {
                        ret = disable_ATA_Security_Password(device, ataPassword, satATASecuritySupported);
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

int run_Freeze_ATA_Security(tDevice *device, bool forceSATvalid, bool forceSAT)
{
    int ret = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (forceSATvalid)
    {
        satATASecuritySupported = forceSAT;
    }
    if (device->drive_info.drive_type == ATA_DRIVE || satATASecuritySupported)
    {
        ataSecurityStatus securityStatus;
        memset(&securityStatus, 0, sizeof(ataSecurityStatus));
        get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
        if (securityStatus.securitySupported)
        {
            if (satATASecuritySupported)//if SAT ATA security supported, use it so the SATL manages the commands.
            {
                ret = scsi_SecurityProtocol_Out(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, SAT_SECURITY_PROTOCOL_SPECIFIC_FREEZE_LOCK, false, 0, NULL, 15);
            }
            else
            {
                ret = ata_Security_Freeze_Lock(device);
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

//Will only unlock the drive
//TODO: Check if security count expired!
int run_Unlock_ATA_Security(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
{
    int ret = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (forceSATvalid)
    {
        satATASecuritySupported = forceSAT;
    }
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
                        printf("Security is Frozen. Cannot Unlock the device.\n");
                    }
                    ret = FROZEN;
                }
                else
                {
                    if (securityStatus.securityLocked)
                    {
                        if (securityStatus.securityCountExpired)
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Password attempts exceeded. You must power cycle the drive to clear the attempt counter and retry the operation.\n");
                            }
                            return FAILURE;
                        }
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Attempting to unlock security with password = ");
                            print_ATA_Security_Password(&ataPassword);
                        }
                        if (SUCCESS == unlock_ATA_Security(device, ataPassword, satATASecuritySupported))
                        {
                            securityStatus.securityLocked = false;
                            ret = SUCCESS;
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Unable to unlock drive with password = ");
                                print_ATA_Security_Password(&ataPassword);
                            }
                        }
                    }
                    else
                    {
                        printf("ATA security is not locked. Nothing to do.\n");
                        ret = SUCCESS;
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
        printf("Not an ATA drive or ATA security protocol is not supported\n");
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int run_Set_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
{
    int ret = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (forceSATvalid)
    {
        satATASecuritySupported = forceSAT;
    }
    if (device->drive_info.drive_type == ATA_DRIVE || satATASecuritySupported)
    {
        ataSecurityStatus securityStatus;
        memset(&securityStatus, 0, sizeof(ataSecurityStatus));
        get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
        if (securityStatus.securitySupported)
        {
            //Check if frozen or already enabled.
            if (securityStatus.securityFrozen)
            {
                //If frozen, we cannot do anything
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Security is Frozen. Cannot set the password.\n");
                }
                ret = FROZEN;
            }
            else if (securityStatus.securityLocked)
            {
                //ATA security is already enabled. The password must be disabled before a new one is set (for user password)
                //Master password should be able to be set.
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Security is Locked. Cannot set a password without unlocking or erasing the device (with the master password).\n");
                }
                ret = FAILURE;
            }
            else
            {
                //set the password!
                //TODO: verbose message here about what password is being set?
                ret = set_ATA_Security_Password(device, ataPassword, satATASecuritySupported);
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

int run_ATA_Security_Erase(tDevice *device, eATASecurityEraseType eraseType,  ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
{
    int result = UNKNOWN;
    bool satATASecuritySupported = sat_ATA_Security_Protocol_Supported(device);
    if (forceSATvalid)
    {
        satATASecuritySupported = forceSAT;
    }
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
            if (!securityStatus.enhancedEraseSupported && eraseType == ATA_SECURITY_ERASE_ENHANCED_ERASE)
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
            if (eraseType == ATA_SECURITY_ERASE_ENHANCED_ERASE)
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
        if (securityStatus.securityCountExpired)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Password attempts exceeded. You must power cycle the drive to clear the attempt counter and retry the operation.\n");
            }
            return FAILURE;
        }
        //ATA spec shows you can erase without unlocking the drive.
        //We may want to put this back in as a "just in case" or a "try it to see if the password was right" though.
//      if (securityStatus.securityLocked && ataPassword.passwordType != ATA_PASSWORD_MASTER)//master shouldn't need to unlock since it can be used to repurpose the drive
//      {
//          if (VERBOSITY_QUIET < device->deviceVerbosity)
//          {
//              printf("Attempting to unlock security with password = ");
//              print_ATA_Security_Password(&ataPassword);
//          }
//          if (SUCCESS == unlock_ATA_Security(device, ataPassword, satATASecuritySupported))
//          {
//              securityStatus.securityLocked = false;
//          }
//          else
//          {
//              if (VERBOSITY_QUIET < device->deviceVerbosity)
//              {
//                  printf("Unable to unlock drive with password = ");
//                  print_ATA_Security_Password(&ataPassword);
//              }
//              return FAILURE;
//          }
//      }
        if (!securityStatus.securityEnabled)//ATA spec is not clear on whether an erase can be done at anytime with the master PW or not, so we'll try to set one for the erase.
        {
            //set the password
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Setting ATA Security password to ");
                print_ATA_Security_Password(&ataPassword);
            }
            if (SUCCESS != set_ATA_Security_Password(device, ataPassword, satATASecuritySupported))
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
            if (eraseType == ATA_SECURITY_ERASE_ENHANCED_ERASE)
            {
                printf("Enhanced ATA Security Erase using ");
            }
            else
            {
                printf("ATA Security Erase using ");
            }
            if (ataPassword.passwordType == ATA_PASSWORD_MASTER)
            {
                printf("Master password: ");
            }
            else
            {
                printf("User password: ");
            }
            print_ATA_Security_Password(&ataPassword);
            if (eraseTimeMinutes == UINT16_MAX || eraseTimeMinutes == 0)
            {
                if (eraseTimeMinutes == 0)
                {
                    printf("\n\tThe drive did not report an erase time estimate.\n");
                    printf("\tA completion estimate is not available for this drive.\n");
                }
                else
                {
                    uint64_t erasemaxSeconds = 0;
                    //TODO: make this print out a friendly looking value like we do in the function that prints out ATA security information
                    printf("\n\tThe drive reported an estimated erase time longer than\n");
                    if (securityStatus.extendedTimeFormat)
                    {
                        printf("\t65532 minutes (max per ATA specification).\n");
                        erasemaxSeconds = 65532 * 60;
                    }
                    else
                    {
                        printf("\t508 minutes (max per ATA specification).\n");
                        erasemaxSeconds = 508 * 2;
                    }
                    //provide a completion time estimate based on the max values.
                    //Need to report it as a time greater than what we print to the screen to make it clear.
                    time_t currentTime = time(NULL);
                    time_t futureTime = get_Future_Date_And_Time(currentTime, C_CAST(uint64_t, eraseTimeMinutes) * UINT64_C(60));
                    uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
                    char timeFormat[TIME_STRING_LENGTH] = { 0 };
                    convert_Seconds_To_Displayable_Time(erasemaxSeconds, NULL, &days, &hours, &minutes, &seconds);
                    printf("\n\tCurrent Time: %s\tDrive reported completion time: >", get_Current_Time_String(C_CAST(const time_t*, &currentTime), timeFormat, TIME_STRING_LENGTH));
                    print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
                    printf("from now.\n");
                    memset(timeFormat, 0, TIME_STRING_LENGTH);//clear this again before reusing it
                    printf("\tEstimated completion Time : sometime after %s", get_Current_Time_String(C_CAST(const time_t*, &futureTime), timeFormat, TIME_STRING_LENGTH));
                }
            }
            else
            {
                time_t currentTime = time(NULL);
                time_t futureTime = get_Future_Date_And_Time(currentTime, C_CAST(uint64_t, eraseTimeMinutes) * UINT64_C(60));
                uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
                char timeFormat[TIME_STRING_LENGTH] = { 0 };
                convert_Seconds_To_Displayable_Time(C_CAST(uint64_t, eraseTimeMinutes) * UINT64_C(60), NULL, &days, &hours, &minutes, &seconds);
                printf("\n\tCurrent Time: %s\tDrive reported completion time: ", get_Current_Time_String(C_CAST(const time_t*, &currentTime), timeFormat, TIME_STRING_LENGTH));
                print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
                printf("from now.\n");
                memset(timeFormat, 0, TIME_STRING_LENGTH);//clear this again before reusing it
                printf("\tEstimated completion Time : %s", get_Current_Time_String(C_CAST(const time_t*, &futureTime), timeFormat, TIME_STRING_LENGTH));
            }
            printf("\n\tPlease DO NOT remove power to the drive during the erase\n");
            printf("\tas this will leave it in an uninitialized state with the password set.\n");
            printf("\tIf the power is removed, rerun this test with your utility.\n");
            printf("\tUpon erase completion, the password is automatically cleared.\n\n");
        }
        seatimer_t ataSecureEraseTimer;
        memset(&ataSecureEraseTimer, 0, sizeof(seatimer_t));
        uint32_t timeout = 0;
        if (os_Is_Infinite_Timeout_Supported())
        {
            timeout = INFINITE_TIMEOUT_VALUE;
        }
        else
        {
            timeout = MAX_CMD_TIMEOUT_SECONDS;
        }
        os_Lock_Device(device);
        start_Timer(&ataSecureEraseTimer);
        int ataEraseResult = start_ATA_Security_Erase(device, ataPassword, eraseType, timeout, satATASecuritySupported);
        stop_Timer(&ataSecureEraseTimer);
        os_Unlock_Device(device);
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
                //commenting this out because a controller could change this data from what the drive reported and we don't want to store that.
                //memcpy((uint8_t*)&device->drive_info.IdentifyData.ata.Word000, &ataVPDPage[60], LEGACY_DRIVE_SEC_SIZE);
            }
        }
        //issue an identify device command before we read the ATA security bits to make sure the data isn't stale in our structure.
        ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
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
            double ataSecureEraseTimerSeconds = get_Seconds(ataSecureEraseTimer);
            convert_Seconds_To_Displayable_Time(C_CAST(uint64_t, ataSecureEraseTimerSeconds), &years, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
            printf("\n\n");
        }
        if (result == FAILURE || hostResetDuringErase)
        {
            //disable the password if it's enabled
            if (securityStatus.securityLocked)
            {
                unlock_ATA_Security(device, ataPassword, satATASecuritySupported);
                memset(&securityStatus, 0, sizeof(ataSecurityStatus));
                get_ATA_Security_Info(device, &securityStatus, satATASecuritySupported);
            }
            if (securityStatus.securityEnabled && !securityStatus.securityLocked)
            {
                if (SUCCESS == disable_ATA_Security_Password(device, ataPassword, satATASecuritySupported))
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
                        printf("\tWARNING!!! Unable to remove the ATA security password used during erase!!\n");
                        printf("\tErase password that was used was: ");
                        print_ATA_Security_Password(&ataPassword);
                        printf("\n");
                    }
                }
            }
            else if(VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\tWARNING!!! The drive is in a security state where clearing the password is not possible!\n");
                printf("\tPlease power cycle the drive and try clearing the password upon powerup.\n");
                printf("\tErase password that was used was: ");
                print_ATA_Security_Password(&ataPassword);
                printf("\n");
            }
            if (hostResetDuringErase)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\tThe host reset the drive during the erase.\n\tEnsure no other applications are trying to access\n\tthe drive while it is erasing.\n\n");
                }
            }
        }
        os_Update_File_System_Cache(device);
    }
    return result;
}
