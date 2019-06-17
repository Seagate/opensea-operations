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
// \file trim_unmap.c

#include "trim_unmap.h"

bool is_ATA_Data_Set_Management_XL_Supported(tDevice * device)
{
    bool supported = false;
    if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        uint8_t logBuffer[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_PAGES, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            bool supportedCapabilities = false;
            //find the supported capabilities page in the list, then read it
            uint8_t pageNumber = logBuffer[2];
            uint16_t revision = M_BytesTo2ByteValue(logBuffer[1], logBuffer[0]);
            if (pageNumber == (uint8_t)ATA_ID_DATA_LOG_SUPPORTED_PAGES && revision >= 0x0001)
            {
                //data is valid, so figure out supported pages
                uint8_t listLen = logBuffer[8];
                for (uint16_t iter = 9; iter < (uint16_t)(listLen + 8) && iter < LEGACY_DRIVE_SEC_SIZE; ++iter)
                {
                    switch (logBuffer[iter])
                    {
                    case ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES:
                        supportedCapabilities = true;
                        break;
                    case ATA_ID_DATA_LOG_SUPPORTED_PAGES:
                    case ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA:
                    case ATA_ID_DATA_LOG_CAPACITY:
                    case ATA_ID_DATA_LOG_CURRENT_SETTINGS:
                    case ATA_ID_DATA_LOG_ATA_STRINGS:
                    case ATA_ID_DATA_LOG_SECURITY:
                    case ATA_ID_DATA_LOG_PARALLEL_ATA:
                    case ATA_ID_DATA_LOG_SERIAL_ATA:
                    case ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION:
                        break;
                    default:
                        break;
                    }
                }
            }
            if (supportedCapabilities)
            {
                memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4], logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES && M_Word0(qword0) >= 0x0001)
                    {
                        uint64_t supportedCapabilities = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (supportedCapabilities & BIT63)
                        {
                            if (supportedCapabilities & BIT50)
                            {
                                supported = true;
                            }
                        }
                    }
                }
            }
        }
    }
    //NOTE: we can add reading the ID Data log through SMART read log, but there is no point. Everything should have it in GPL that would support this command.
    return supported;
}

bool is_Trim_Or_Unmap_Supported(tDevice *device, uint32_t *maxTrimOrUnmapBlockDescriptors, uint32_t *maxLBACount)
{
    bool supported = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.IdentifyData.ata.Word169 & BIT0)
        {
            supported = true;
        }
        if (NULL != maxTrimOrUnmapBlockDescriptors)
        {
            *maxTrimOrUnmapBlockDescriptors = device->drive_info.IdentifyData.ata.Word105 * 64;//multiple by 64 since you can fit a maximum of 64 descriptors in each 512 byte block
        }
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
        {
            supported = true;

            if (maxTrimOrUnmapBlockDescriptors && maxLBACount)
            {
                //Max of 256, 16byte ranges specified in a single command
                *maxTrimOrUnmapBlockDescriptors = 256;
                *maxLBACount = UINT32_MAX;
#if defined (_WIN32)
                //in Windows we rely on translation through SCSI unmap, so we need to meet the limitations we're given in it...-TJE
                //TODO: If we find other OS's with limitations we may need to change the #if or use some other kind of check instead.
                if (NULL != maxTrimOrUnmapBlockDescriptors && NULL != maxLBACount)
                {
                    uint8_t *blockLimits = (uint8_t*)calloc(VPD_BLOCK_LIMITS_LEN, sizeof(uint8_t));
                    if (!blockLimits)
                    {
                        perror("calloc failure!");
                        return supported;
                    }
                    if (SUCCESS == scsi_Inquiry(device, blockLimits, VPD_BLOCK_LIMITS_LEN, BLOCK_LIMITS, true, false))
                    {
                        *maxTrimOrUnmapBlockDescriptors = M_BytesTo4ByteValue(blockLimits[24], blockLimits[25], blockLimits[26], blockLimits[27]);
                        *maxLBACount = M_BytesTo4ByteValue(blockLimits[20], blockLimits[21], blockLimits[22], blockLimits[23]);
                    }
                    safe_Free(blockLimits);
                }
#endif
            }
        }
        break;
#endif
    case SCSI_DRIVE:
    {
        //check the bit in logical block provisioning VPD page
        uint8_t *lbpPage = (uint8_t*)calloc(VPD_LOGICAL_BLOCK_PROVISIONING_LEN, sizeof(uint8_t));
        if (NULL == lbpPage)
        {
            perror("calloc failure!");
            return false;
        }
        if (SUCCESS == scsi_Inquiry(device, lbpPage, VPD_LOGICAL_BLOCK_PROVISIONING_LEN, LOGICAL_BLOCK_PROVISIONING, true, false))
        {
            if ((lbpPage[5] & BIT7) > 0)
            {
                supported = true;
            }
        }
        safe_Free(lbpPage);
        if (supported == true && NULL != maxTrimOrUnmapBlockDescriptors && NULL != maxLBACount)
        {
            uint8_t *blockLimits = (uint8_t*)calloc(VPD_BLOCK_LIMITS_LEN, sizeof(uint8_t));
            if (NULL == blockLimits)
            {
                perror("calloc failure!");
                return supported;
            }
            if (SUCCESS == scsi_Inquiry(device, blockLimits, VPD_BLOCK_LIMITS_LEN, BLOCK_LIMITS, true, false))
            {
                *maxTrimOrUnmapBlockDescriptors = M_BytesTo4ByteValue(blockLimits[24], blockLimits[25], blockLimits[26], blockLimits[27]);
                *maxLBACount = M_BytesTo4ByteValue(blockLimits[20], blockLimits[21], blockLimits[22], blockLimits[23]);
            }
            safe_Free(blockLimits);
        }
    }
    break;
    default:
        break;
    }
    return supported;
}

int trim_Unmap_Range(tDevice *device, uint64_t startLBA, uint64_t range)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Trim_Range(device, startLBA, range);
        break;
    case NVME_DRIVE:
#if !defined(DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Deallocate_Range(device, startLBA, range);
        break;
#endif
    case SCSI_DRIVE:
        ret = scsi_Unmap_Range(device, startLBA, range);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
int nvme_Deallocate_Range(tDevice *device, uint64_t startLBA, uint64_t range)
{
    int ret = UNKNOWN;
    uint32_t maxTrimOrUnmapBlockDescriptors = 0, maxLBACount = 0;
    if (is_Trim_Or_Unmap_Supported(device, &maxTrimOrUnmapBlockDescriptors, &maxLBACount))
    {
        //We SHOULD only need one command since each range is a uint32 and more than large enough to get a majority of the drive. Maybe even get a whole drive done in 2 ranges...
        //BUT we may be limited by the OS.
        //TODO: handle limitations to maxLBACount & maxTrimOrUnmapBlockDescriptors. This makes this funciton MUCH more complicated than it currently is...basically would look like a SCSI unmap command below - TJE
        uint32_t contextAttributes = 0;//this is here in case we want to enable setting these bits some time later. - TJE
        uint8_t deallocate[4096] = { 0 };//This will hold the maximum number of ranges/descriptors we can.
        uint32_t deallocateRange = (uint32_t)M_Min(M_Min(range, UINT32_MAX), maxLBACount);
        uint64_t finalLBA = startLBA + range;
        uint16_t descriptorCount = 0;
        for (uint64_t deallocateLBA = startLBA, offset = 0; deallocateLBA < finalLBA && descriptorCount <= maxTrimOrUnmapBlockDescriptors; deallocateLBA += deallocateRange, offset += 16)
        {
            //context attributes
            deallocate[offset + 0] = M_Byte3(contextAttributes);
            deallocate[offset + 1] = M_Byte2(contextAttributes);
            deallocate[offset + 2] = M_Byte1(contextAttributes);
            deallocate[offset + 3] = M_Byte0(contextAttributes);
            //range/length in LBAs
            deallocate[offset + 4] = M_Byte3(deallocateRange);
            deallocate[offset + 5] = M_Byte2(deallocateRange);
            deallocate[offset + 6] = M_Byte1(deallocateRange);
            deallocate[offset + 7] = M_Byte0(deallocateRange);
            //starting LBA
            deallocate[offset + 8] = M_Byte7(deallocateLBA);
            deallocate[offset + 9] = M_Byte6(deallocateLBA);
            deallocate[offset + 10] = M_Byte5(deallocateLBA);
            deallocate[offset + 11] = M_Byte4(deallocateLBA);
            deallocate[offset + 12] = M_Byte3(deallocateLBA);
            deallocate[offset + 13] = M_Byte2(deallocateLBA);
            deallocate[offset + 14] = M_Byte1(deallocateLBA);
            deallocate[offset + 15] = M_Byte0(deallocateLBA);

            ++descriptorCount;
        }
        //send the command(s) to the drive....currently only 1 command to do this. May need to revisit later - TJE
        ret = nvme_Dataset_Management(device, (uint8_t)(descriptorCount - 1), true, false, false, deallocate, 4096);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}
#endif

int ata_Trim_Range(tDevice *device, uint64_t startLBA, uint64_t range)
{
    int ret = UNKNOWN;
    uint32_t maxTrimOrUnmapBlockDescriptors = 0, maxLBACount = 0;
    if (is_Trim_Or_Unmap_Supported(device, &maxTrimOrUnmapBlockDescriptors, &maxLBACount))
    {
        bool xlCommand = is_ATA_Data_Set_Management_XL_Supported(device);
        uint64_t maxRange = xlCommand ? UINT64_MAX : UINT16_MAX;//xl = uint64_max, regular = uint16_max
        uint16_t maxEntriesPerPage = xlCommand ? 32 : 64;//regular cmd = 64, regular = 32
        uint8_t entryLengthBytes = xlCommand ? 16 : 8;
        uint64_t finalLBA = startLBA + range;
        uint64_t numberOfLBAsToTrim = ((startLBA + range) - startLBA);
        uint64_t trimRange = (uint16_t)M_Min(numberOfLBAsToTrim, maxRange);//range must be FFFFh or less, so take the minimum of these two
        uint64_t trimDescriptors = ((numberOfLBAsToTrim + trimRange) - 1) / trimRange;//need to make sure this division rounds up as necessary so the whole range gets TRIMed
        uint64_t trimBufferLen = (uint64_t)((((trimDescriptors + maxEntriesPerPage) - 1) / maxEntriesPerPage) * LEGACY_DRIVE_SEC_SIZE);//maximum of 64 TRIM entries per sector
        uint8_t *trimBuffer = (uint8_t*)calloc((size_t)trimBufferLen, sizeof(uint8_t));
        uint64_t trimLBA = 0;
        uint64_t bufferIter = 0;
        uint16_t trimCommands = 0;
        uint16_t maxTRIMdataBlocks = (uint16_t)(maxTrimOrUnmapBlockDescriptors / maxEntriesPerPage);
        uint16_t numberOfTRIMCommandsRequired = (uint16_t)((((trimBufferLen / LEGACY_DRIVE_SEC_SIZE) + maxTRIMdataBlocks) - 1) / maxTRIMdataBlocks);
        uint32_t trimCommandLen = (uint32_t)(M_Min(maxTRIMdataBlocks * LEGACY_DRIVE_SEC_SIZE, trimBufferLen));
        if (!trimBuffer)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        //fill in the data buffer for a TRIM command
        for (trimLBA = startLBA; trimLBA < finalLBA && (bufferIter + (xlCommand - 1)) < trimBufferLen; trimLBA += trimRange, bufferIter += entryLengthBytes)
        {
            //make sure we don't go beyond the ending LBA for our TRIM, so adjust the range to fit
            if ((trimLBA + trimRange) > finalLBA)
            {
                trimRange = (finalLBA - trimLBA);
            }
            //set the LBA
            trimBuffer[bufferIter] = M_Byte0(trimLBA);
            trimBuffer[bufferIter + 1] = M_Byte1(trimLBA);
            trimBuffer[bufferIter + 2] = M_Byte2(trimLBA);
            trimBuffer[bufferIter + 3] = M_Byte3(trimLBA);
            trimBuffer[bufferIter + 4] = M_Byte4(trimLBA);
            trimBuffer[bufferIter + 5] = M_Byte5(trimLBA);
            //set the range
            if (xlCommand)
            {
                trimBuffer[bufferIter + 6] = RESERVED;
                trimBuffer[bufferIter + 7] = RESERVED;
                trimBuffer[bufferIter + 8] = M_Byte0(trimRange);
                trimBuffer[bufferIter + 9] = M_Byte1(trimRange);
                trimBuffer[bufferIter + 10] = M_Byte2(trimRange);
                trimBuffer[bufferIter + 11] = M_Byte3(trimRange);
                trimBuffer[bufferIter + 12] = M_Byte4(trimRange);
                trimBuffer[bufferIter + 13] = M_Byte5(trimRange);
                trimBuffer[bufferIter + 14] = M_Byte6(trimRange);
                trimBuffer[bufferIter + 15] = M_Byte7(trimRange);
            }
            else
            {
                trimBuffer[bufferIter + 6] = M_Byte0(trimRange);
                trimBuffer[bufferIter + 7] = M_Byte1(trimRange);
            }
        }
        //send the command(s)
#if defined(_DEBUG)
        printf("TRIM buffer size: %"PRIu64"\n", trimBufferLen);
#endif
        uint32_t trimOffset = 0;
        for (trimCommands = 0; trimCommands < numberOfTRIMCommandsRequired; trimCommands++)
        {
#if defined(_DEBUG)
            printf("TRIM Offset: %"PRIu32"\n", trimOffset);
#endif
            if ((trimCommandLen * (trimCommands + 1)) > trimBufferLen)
            {
                trimCommandLen = (uint32_t)(trimBufferLen - (trimCommandLen * trimCommands));
            }
            if (ata_Data_Set_Management(device, true, &trimBuffer[trimOffset], trimCommandLen, xlCommand) != SUCCESS)
            {
                ret = FAILURE;
                break;
            }
            else
            {
                ret = SUCCESS;
            }
            trimOffset += trimCommandLen;
        }
#if defined(_DEBUG)
        printf("TRIM Offset: %"PRIu32"\n", trimOffset);
#endif
        free(trimBuffer);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int scsi_Unmap_Range(tDevice *device, uint64_t startLBA, uint64_t range)
{
    int ret = UNKNOWN;
    uint32_t maxTrimOrUnmapBlockDescriptors = 0, maxLBACount = 0;
    if (is_Trim_Or_Unmap_Supported(device, &maxTrimOrUnmapBlockDescriptors, &maxLBACount))
    {
        //create the buffer first, then send the command
        uint32_t unmapRange = (uint32_t)(M_Min(((startLBA + range) - startLBA), maxLBACount));//this may truncate but that is expected since the maximum range you can specify for a UNMAP command is 0xFFFFFFFF
        uint32_t unmapDescriptors = (uint32_t)(((((startLBA + range) - startLBA) + unmapRange) - 1) / unmapRange);//need to round this division up o we won't unmap the whole range that was requested
        uint32_t unmapBufferLen = unmapDescriptors * 16;//this is JUST the descriptors. The header will need to be tacked onto the beginning of this for each command.
        uint8_t *unmapBuffer = (uint8_t*)calloc(unmapBufferLen, sizeof(uint8_t));
        uint64_t unmapLBA = 0;
        uint64_t bufferIter = 0;
        uint32_t unmapCommands = 0;
        uint32_t numberOfUnmapCommandsRequired = ((unmapDescriptors + maxTrimOrUnmapBlockDescriptors) - 1) / maxTrimOrUnmapBlockDescriptors;
        uint32_t unmapCommandDataLen = 0;
        uint32_t descriptorsPerCommand = 0;
        if (unmapRange == maxLBACount)
        {
            numberOfUnmapCommandsRequired = unmapDescriptors;
        }
        descriptorsPerCommand = M_Min(maxTrimOrUnmapBlockDescriptors, unmapDescriptors / numberOfUnmapCommandsRequired);
        unmapCommandDataLen = M_Min(descriptorsPerCommand * 16, unmapBufferLen) + 8;//add 8 for the length of the header
        if (unmapBuffer == NULL)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        //unmap block descriptors
        for (unmapLBA = startLBA, bufferIter = 0; unmapLBA < (startLBA + range) && bufferIter < unmapBufferLen; unmapLBA += unmapRange, bufferIter += 16)
        {
            //make sure we don't go beyond the ending LBA for our TRIM, so adjust the range to fit
            if (unmapLBA + unmapRange >(startLBA + range))
            {
                unmapRange = (uint32_t)((startLBA + range) - unmapLBA);
            }
            //set the LBA
            unmapBuffer[bufferIter + 0] = (uint8_t)(unmapLBA >> 56);
            unmapBuffer[bufferIter + 1] = (uint8_t)(unmapLBA >> 48);
            unmapBuffer[bufferIter + 2] = (uint8_t)(unmapLBA >> 40);
            unmapBuffer[bufferIter + 3] = (uint8_t)(unmapLBA >> 32);
            unmapBuffer[bufferIter + 4] = (uint8_t)(unmapLBA >> 24);
            unmapBuffer[bufferIter + 5] = (uint8_t)(unmapLBA >> 16);
            unmapBuffer[bufferIter + 6] = (uint8_t)(unmapLBA >> 8);
            unmapBuffer[bufferIter + 7] = (uint8_t)(unmapLBA & UINT8_MAX);
            //set the range
            unmapBuffer[bufferIter + 8] = (uint8_t)(unmapRange >> 24);
            unmapBuffer[bufferIter + 9] = (uint8_t)(unmapRange >> 16);
            unmapBuffer[bufferIter + 10] = (uint8_t)(unmapRange >> 8);
            unmapBuffer[bufferIter + 11] = (uint8_t)(unmapRange & UINT8_MAX);
            //reserved
            unmapBuffer[bufferIter + 12] = RESERVED;
            unmapBuffer[bufferIter + 13] = RESERVED;
            unmapBuffer[bufferIter + 14] = RESERVED;
            unmapBuffer[bufferIter + 15] = RESERVED;
        }
        //send the unmap command(s) to the device
#if defined(_DEBUG)
        printf("UNMAP buffer size: %"PRIu32"\n", unmapBufferLen);
#endif
        uint32_t unmapOffset = 0;
        //allocate a buffer to hold the descriptors AND header. Earlier buffer was just to build the descriptors into it.
        uint8_t* unmapCommandBuffer = (uint8_t*)calloc(unmapCommandDataLen, sizeof(uint8_t));
        if (NULL == unmapCommandBuffer)
        {
            perror("calloc failure");
            return MEMORY_FAILURE;
        }
        for (unmapCommands = 0; unmapCommands < numberOfUnmapCommandsRequired; unmapCommands++)
        {
#if defined(_DEBUG)
            printf("UNMAP offset: %"PRIu32"\n", unmapOffset);
#endif
            //adjust the size to allocate a buffer based on the remaining number of descriptors in the buffer
            if (((unmapCommandDataLen - 8) * (unmapCommands + 1)) > unmapBufferLen)
            {
                unmapCommandDataLen = unmapBufferLen - ((unmapCommandDataLen - 8) * (unmapCommands + 1));
                unmapCommandDataLen += 8;
                uint8_t *temp = realloc(unmapCommandBuffer, unmapCommandDataLen * sizeof(uint8_t));
                if (temp == NULL)
                {
                    perror("realloc failure!");
                    return MEMORY_FAILURE;
                }
                unmapCommandBuffer = temp;
                memset(unmapCommandBuffer, 0, unmapCommandDataLen);
            }
            //fill in the data buffer for a UNMAP command with the header
            //unmap data length
            unmapCommandBuffer[0] = (uint8_t)((unmapCommandDataLen - 2) >> 8);
            unmapCommandBuffer[1] = (uint8_t)((unmapCommandDataLen & 0x0000FFFF) - 2);
            //unmap block descriptor data length (number of entries * 16...this should ALWAYS fit inside the buffer length we calculated earlier)
            unmapCommandBuffer[2] = (uint8_t)((unmapCommandDataLen - 8) >> 8);
            unmapCommandBuffer[3] = (uint8_t)((unmapCommandDataLen - 8) & 0x0000FFFF);
            //reserved
            unmapCommandBuffer[4] = RESERVED;
            unmapCommandBuffer[5] = RESERVED;
            unmapCommandBuffer[6] = RESERVED;
            unmapCommandBuffer[7] = RESERVED;
            //now copy the number of descriptors for this command into the allocated buffer
            memcpy(&unmapCommandBuffer[8], &unmapBuffer[unmapOffset], (unmapCommandDataLen - 8));
            //send the command
            if (SUCCESS != scsi_Unmap(device, false, 0, (uint16_t)(unmapCommandDataLen), unmapCommandBuffer))
            {
                ret = FAILURE;
                break;
            }
            else
            {
                ret = SUCCESS;
            }
            unmapOffset += (unmapCommandDataLen - 8);
            memset(unmapCommandBuffer, 0, unmapCommandDataLen);
        }
#if defined(_DEBUG)
        printf("UNMAP offset: %"PRIu32"\n", unmapOffset);
#endif
        if (unmapCommandBuffer != NULL)
        {
            free(unmapCommandBuffer);
        }
        if (unmapBuffer != NULL)
        {
            free(unmapBuffer);
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}
