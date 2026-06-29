// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file buffer_test.c
// \brief This file defines the function calls for performing buffer/cabling tests

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "pattern_utils.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "buffer_test.h"
#include "cmds.h"
#include "operations.h"

static bool ata_Buffer_Commands_Supported(const tDevice* M_NONNULL device)
{
    bool supported = false;
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT13 &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT12) ||
        (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT13 &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT12))
    {
        // PIO commands
        supported = true;
    }
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word053)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word053) & BIT1) /* this is a validity bit for field 69 */
        && (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word069)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word069) & BIT11 &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word069) & BIT10))
    {
        // DMA commands
        supported = true;
    }
    return supported;
}

static bool scsi_Buffer_Commands_Supported(const tDevice* M_NONNULL device)
{
    bool supported = false;
    // SCSI 2 + should support this.
    // SCSI 1 probably won't...but this is so old it may not be a problem
    // Only asking about read buffer command, since write buffer will likely be implemented for at least FWDL, so if
    // this is supported, the equivalent write buffer command should also be supported
    scsiOperationCodeInfoRequest readBufSupReq;
    M_INITIALIZE_STRUCTURE(&readBufSupReq, sizeof(scsiOperationCodeInfoRequest));
    readBufSupReq.operationCode      = READ_BUFFER_CMD;
    readBufSupReq.serviceActionValid = false;
    eSCSICmdSupport readBufSupport   = is_SCSI_Operation_Code_Supported(device, &readBufSupReq);
    if (readBufSupport == SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD)
    {
        supported = true;
    }
    else
    {
        // this means the command to ask about support didn't work, so we're just going to try asking the size of
        // the buffer and if that works, it is supported
        DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedCommandData, 4);
        if (SUCCESS == scsi_Read_Buffer(device, SCSI_RB_DESCRIPTOR, 0, 0, 4, supportedCommandData))
        {
            supported = true;
        }
    }
    return supported;
}

static bool are_Buffer_Commands_Available(const tDevice* M_NONNULL device)
{
    bool supported = false;
    // Check if read/write buffer commands are supported on SATA and SAS
    if (get_Device_DriveType(device) == ATA_DRIVE)
    {
        supported = ata_Buffer_Commands_Supported(device);
    }
    else if (get_Device_DriveType(device) == SCSI_DRIVE)
    {
        supported = scsi_Buffer_Commands_Supported(device);
    }
    return supported;
}

static eReturnValues get_Buffer_Size(const tDevice* device, uint32_t* bufferSize, uint8_t* offsetBoundary)
{
    eReturnValues ret = SUCCESS;
    if (!bufferSize || !offsetBoundary)
    {
        return BAD_PARAMETER;
    }
    *bufferSize = LEGACY_DRIVE_SEC_SIZE; // default to this size. Change this only if the drive reports a different size
    *offsetBoundary = 0x09;              // default to this size. Change this only if the drive reports a different size
    // get the size of the buffer for the drive.
    if (get_Device_DriveType(device) == SCSI_DRIVE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, bufferSizeData, 4);
        if (SUCCESS == scsi_Read_Buffer(device, SCSI_RB_DESCRIPTOR, 0, 0, 4, bufferSizeData))
        {
            *offsetBoundary = bufferSizeData[0]; // not sure if this is actually needed - TJE
            *bufferSize     = M_BytesTo4ByteValue(0, bufferSizeData[1], bufferSizeData[2], bufferSizeData[3]);
        }
        else
        {
            ret = FAILURE; // this shouldn't happen...
        }
    }
    return ret;
}

static M_INLINE uint32_t get_LBAs_Per_BufferSize(const tDevice* device, uint32_t bufferSize)
{
    uint32_t lbasPerBufferSize = UINT32_C(1);
    if (device->drive_info.deviceBlockSize > 0)
    {
        lbasPerBufferSize = bufferSize / device->drive_info.deviceBlockSize;
    }
    return lbasPerBufferSize;
}

static eReturnValues send_Read_Buffer_Command(const tDevice* device, uint8_t* ptrData, uint32_t dataSize)
{
    if (get_Device_DriveType(device) == ATA_DRIVE)
    {
        // return ata_Read_Buffer(device, ptrData, device->drive_info.ata_Options.readBufferDMASupported);
        // Switching to this new function since it will automatically try DMA mode if supported by the drive.
        // If the controller or driver don't like issuing DMA mode, this will detect it and retry the command with PIO
        // mode.
        return send_ATA_Read_Buffer_Cmd(device, ptrData);
    }
    else if (get_Device_DriveType(device) == SCSI_DRIVE)
    {
        return scsi_Read_Buffer(device, SCSI_RB_DATA, 0, 0, dataSize, ptrData);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

static eReturnValues send_Write_Buffer_Command(const tDevice* device, uint8_t* ptrData, uint32_t dataSize)
{
    if (get_Device_DriveType(device) == ATA_DRIVE)
    {
        // return ata_Write_Buffer(device, ptrData, device->drive_info.ata_Options.writeBufferDMASupported);
        // Switching to this new function since it will automatically try DMA mode if supported by the drive.
        // If the controller or driver don't like issuing DMA mode, this will detect it and retry the command with PIO
        // mode.
        return send_ATA_Write_Buffer_Cmd(device, ptrData);
    }
    else if (get_Device_DriveType(device) == SCSI_DRIVE)
    {
        return scsi_Write_Buffer(device, SCSI_WB_DATA, 0, 0, 0, dataSize, ptrData, false, false, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

static bool was_There_A_CRC_Error_On_Last_Command(const tDevice* M_NONNULL device)
{
    bool    crc            = false;
    bool    checkSenseData = false;
    uint8_t senseKey       = UINT8_C(0);
    uint8_t asc            = UINT8_C(0);
    uint8_t ascq           = UINT8_C(0);
    uint8_t fru            = UINT8_C(0);
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        // No direct CRC error, but need to look into how the media and data integrity errors align as well as the path
        // status's
        return false;
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.lastCommandRTFRs.status & ATA_STATUS_BIT_ERROR) // error bit set
        {
            if (device->drive_info.lastCommandRTFRs.error & ATA_ERROR_BIT_INTERFACE_CRC)
            {
                crc = true;
            }
        }
        if (device->drive_info.ataSenseData.validData && !crc)
        {
            checkSenseData = true;
            senseKey       = device->drive_info.ataSenseData.senseKey;
            asc            = device->drive_info.ataSenseData.additionalSenseCode;
            ascq           = device->drive_info.ataSenseData.additionalSenseCodeQualifier;
        }
    }
    else if (get_Device_DriveType(device) == SCSI_DRIVE)
    {
        checkSenseData = true;
        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                   &fru);
    }
    if (checkSenseData)
    {
        if (senseKey == SENSE_KEY_ABORTED_COMMAND) // INFORMATION UNIT iuCRC ERROR DETECTED
        {
            switch (asc)
            {
            case 0x08:
                if (ascq == 0x03) // LOGICAL UNIT COMMUNICATION CRC ERROR (ULTRA-DMA/32)
                {
                    crc = true;
                }
                break;
            case 0x10:
                if (ascq == 0x10) // ID CRC OR ECC ERROR
                {
                    crc = true;
                }
                break;
            case 0x11:
                if (ascq == 0x0D) // DE-COMPRESSION CRC ERROR
                {
                    crc = true;
                }
                break;
            case 0x47:
                switch (ascq)
                {
                case 0x01: // DATA PHASE CRC ERROR DETECTED
                case 0x03: // INFORMATION UNIT iuCRC ERROR DETECTED - SAT will translate a CRC error into this!
                           // Definitely need this one. Less sure about the others...-TJE
                case 0x05: // PROTOCOL SERVICE CRC ERROR
                    crc = true;
                    break;
                default:
                    break;
                }
                break;
            case 0x4B:
                if (ascq == 0x12) // PCIE ECRC CHECK FAILED
                {
                    crc = true;
                }
                break;
            default:
                break;
            }
        }
    }
    return crc;
}

M_NODISCARD_REASON("This function tells whether the test should continue or not. You must use this result to determine "
                   "when to continue testing or exit.")
static M_INLINE bool set_cmd_results(eReturnValues result, ptrPatternTestResults testResults, const tDevice* device)
{
    bool continueTest = true;
    switch (result)
    {
    case OS_PASSTHROUGH_FAILURE:
    case NOT_SUPPORTED:
        continueTest = false;
        break;
    case OS_COMMAND_TIMEOUT:
        ++(testResults->totalCommandTimeouts);
        break;
    case SUCCESS:
        break;
    case ABORTED:
    case COMMAND_FAILURE:
    case FAILURE:
    default:
        if (was_There_A_CRC_Error_On_Last_Command(device))
        {
            ++(testResults->totalCommandCRCErrors);
        }
        break;
    }
    return continueTest;
}

M_NODISCARD static M_INLINE bool write_read_compare_pattern(const tDevice*        device,
                                                            uint8_t*              patternBuffer,
                                                            uint32_t              deviceBufferSize,
                                                            uint64_t              lba,
                                                            uint64_t              lbaRange,
                                                            ptrPatternTestResults testResults,
                                                            eCableTestMode        testMode,
                                                            fuaCmd                fuaCmdReq)
{
    bool          success        = true;
    eReturnValues wbResult       = SUCCESS;
    eReturnValues rbResult       = SUCCESS;
    uint64_t      maxLbaForRange = lba + lbaRange;
    uint64_t      lbaIncrement   = get_LBAs_Per_BufferSize(device, deviceBufferSize);
    for (uint64_t lbaIter = lba; lbaIter < maxLbaForRange; lbaIter += lbaIncrement)
    {
        if ((lbaIter + lbaIncrement) > maxLbaForRange)
        {
            lbaIncrement = maxLbaForRange - lbaIter;
            if (lbaIncrement == 0)
            {
                break;
            }
        }
        switch (testMode)
        {
        case CABLE_TEST_MODE_BUFFER_CMDS:
            wbResult = send_Write_Buffer_Command(device, patternBuffer, deviceBufferSize);
            break;
        case CABLE_TEST_MODE_READ_WRITE_CMDS:
            wbResult = write_LBA(device, lbaIter, fuaCmdReq.writeFUA, patternBuffer, deviceBufferSize);
            break;
        }
        ++(testResults->totalCommandsSent);
        if (!set_cmd_results(wbResult, testResults, device))
        {
            success = false;
            break;
        }
    }
    if (success)
    {
        uint8_t* returnBuffer = safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment);
        if (returnBuffer == M_NULLPTR)
        {
            return false;
        }
        if (testMode == CABLE_TEST_MODE_READ_WRITE_CMDS)
        {
            // flush all writes to media in case anything was cached.
            flush_Cache(device);
        }
        lbaIncrement = get_LBAs_Per_BufferSize(device, deviceBufferSize);
        // now read back the pattern
        for (uint64_t lbaIter = lba; lbaIter < maxLbaForRange; lbaIter += lbaIncrement)
        {
            if ((lbaIter + lbaIncrement) > maxLbaForRange)
            {
                lbaIncrement = maxLbaForRange - lbaIter;
                if (lbaIncrement == 0)
                {
                    break;
                }
            }
            safe_memset(returnBuffer, deviceBufferSize, 0, deviceBufferSize);
            switch (testMode)
            {
            case CABLE_TEST_MODE_BUFFER_CMDS:
                rbResult = send_Read_Buffer_Command(device, returnBuffer, deviceBufferSize);
                break;
            case CABLE_TEST_MODE_READ_WRITE_CMDS:
                rbResult = read_LBA(device, lbaIter, fuaCmdReq.readFUA, returnBuffer, deviceBufferSize);
                break;
            }
            ++(testResults->totalCommandsSent);
            if (!set_cmd_results(rbResult, testResults, device))
            {
                success = false;
            }
            else
            {
                ++(testResults->totalBufferComparisons);
                // first check if the pattern matches or not
                if (memcmp(patternBuffer, returnBuffer, deviceBufferSize) != 0)
                {
                    ++(testResults->totalBufferMiscompares);
                }
            }
        }
        safe_free_aligned(&returnBuffer);
    }
    return success;
}

// Function for simple byte pattern tests. take counter for number of times to try it?
static bool perform_Byte_Pattern_Test(const tDevice*        device,
                                      uint32_t              pattern,
                                      uint32_t              deviceBufferSize,
                                      ptrPatternTestResults testResults,
                                      eCableTestMode        testMode,
                                      uint64_t              lba,
                                      uint64_t              lbarange,
                                      fuaCmd                fuaCmdReq)
{
    bool     result = false;
    uint8_t* patternBuffer =
        C_CAST(uint8_t*,
               safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment)); // only send this to the drive
    if (patternBuffer != M_NULLPTR)
    {
        DECLARE_SEATIMER(patternTimer);
        fill_Pattern_Buffer_Into_Another_Buffer(C_CAST(uint8_t*, &pattern), sizeof(uint32_t), patternBuffer,
                                                deviceBufferSize); // sets the pattern to write into memory
        start_Timer(&patternTimer);
        result = write_read_compare_pattern(device, patternBuffer, deviceBufferSize, lba, lbarange, testResults,
                                            testMode, fuaCmdReq);
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_free_aligned(&patternBuffer);
    return result;
}

typedef enum eRowBoatPatternEnum
{
    ROW_BOAT_PATTERN_00,
    ROW_BOAT_PATTERN_55,
    ROW_BOAT_PATTERN_AA,
    ROW_BOAT_PATTERN_FF
} eRowBoatPattern;

typedef struct s_rowBoatPatternSequence
{
    eRowBoatPattern pat1;
    eRowBoatPattern pat2;
    eRowBoatPattern pat3;
    eRowBoatPattern pat4;
} rowBoatPatternSequence;

rowBoatPatternSequence seq1 = {ROW_BOAT_PATTERN_55, ROW_BOAT_PATTERN_FF, ROW_BOAT_PATTERN_AA, ROW_BOAT_PATTERN_FF};
rowBoatPatternSequence seq2 = {ROW_BOAT_PATTERN_55, ROW_BOAT_PATTERN_00, ROW_BOAT_PATTERN_AA, ROW_BOAT_PATTERN_00};
rowBoatPatternSequence seq3 = {ROW_BOAT_PATTERN_FF, ROW_BOAT_PATTERN_55, ROW_BOAT_PATTERN_FF, ROW_BOAT_PATTERN_AA};
rowBoatPatternSequence seq4 = {ROW_BOAT_PATTERN_00, ROW_BOAT_PATTERN_55, ROW_BOAT_PATTERN_00, ROW_BOAT_PATTERN_AA};

M_NODISCARD static bool rowboat_wrc(const tDevice*        device,
                                    uint32_t              pattern,
                                    uint8_t*              patternBuffer,
                                    uint32_t              deviceBufferSize,
                                    uint64_t              lba,
                                    uint64_t              lbaRange,
                                    ptrPatternTestResults testResults,
                                    eCableTestMode        testMode,
                                    fuaCmd                fuaCmdReq)
{
    fill_Pattern_Buffer_Into_Another_Buffer(C_CAST(uint8_t*, &pattern), sizeof(uint32_t), patternBuffer,
                                            deviceBufferSize); // sets the pattern to write into memory
    if (!write_read_compare_pattern(device, patternBuffer, deviceBufferSize, lba, lbaRange, testResults, testMode,
                                    fuaCmdReq))
    {
        return false;
    }
    return true;
}

static uint32_t set_Rowboat_Pattern_From_Enum(eRowBoatPattern pat)
{
    uint32_t retpat = UINT32_C(0);
    switch (pat)
    {
    case ROW_BOAT_PATTERN_00:
        retpat = UINT32_C(0x00000000);
        break;
    case ROW_BOAT_PATTERN_55:
        retpat = UINT32_C(0x55555555);
        break;
    case ROW_BOAT_PATTERN_AA:
        retpat = UINT32_C(0xAAAAAAAA);
        break;
    case ROW_BOAT_PATTERN_FF:
        retpat = UINT32_C(0xFFFFFFFF);
        break;
    }
    return retpat;
}

static bool perform_RowBoat_Pattern_Test(const tDevice*         device,
                                         rowBoatPatternSequence patternSequence,
                                         uint32_t               deviceBufferSize,
                                         ptrPatternTestResults  testResults,
                                         eCableTestMode         testMode,
                                         uint64_t               lba,
                                         uint64_t               lbaRange,
                                         fuaCmd                 fuaCmdReq)
{
    bool     result = false;
    uint8_t* patternBuffer =
        C_CAST(uint8_t*,
               safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment)); // only send this to the drive
    if (patternBuffer != M_NULLPTR)
    {
        DECLARE_SEATIMER(patternTimer);
        result = true;
        start_Timer(&patternTimer);
        if (!rowboat_wrc(device, set_Rowboat_Pattern_From_Enum(patternSequence.pat1), patternBuffer, deviceBufferSize,
                         lba, lbaRange, testResults, testMode, fuaCmdReq))
        {
            result = false;
        }
        if (!rowboat_wrc(device, set_Rowboat_Pattern_From_Enum(patternSequence.pat2), patternBuffer, deviceBufferSize,
                         lba, lbaRange, testResults, testMode, fuaCmdReq))
        {
            result = false;
        }
        if (!rowboat_wrc(device, set_Rowboat_Pattern_From_Enum(patternSequence.pat3), patternBuffer, deviceBufferSize,
                         lba, lbaRange, testResults, testMode, fuaCmdReq))
        {
            result = false;
        }
        if (!rowboat_wrc(device, set_Rowboat_Pattern_From_Enum(patternSequence.pat4), patternBuffer, deviceBufferSize,
                         lba, lbaRange, testResults, testMode, fuaCmdReq))
        {
            result = false;
        }
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_free_aligned(&patternBuffer);
    return result;
}

static void fill_mark_pattern_in_buffer(uint8_t* patternBuffer, uint32_t deviceBufferSize)
{
#define MARK_PATTERN_SEG_LEN   (48)
#define MARK_PATTERN_TOTAL_LEN (MARK_PATTERN_SEG_LEN * 2)
    uint32_t iter = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, mark0, MARK_PATTERN_SEG_LEN);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, markF, MARK_PATTERN_SEG_LEN);
    uint8_t* mark = mark0;
    safe_memset(markF, MARK_PATTERN_SEG_LEN, 0xFF, MARK_PATTERN_SEG_LEN);
    while (iter < deviceBufferSize)
    {
        safe_memcpy(&patternBuffer[iter], deviceBufferSize - iter, mark,
                    M_Min(MARK_PATTERN_SEG_LEN, deviceBufferSize - iter));
        iter += MARK_PATTERN_SEG_LEN;
        if (mark == mark0)
        {
            mark = markF;
        }
        else
        {
            mark = mark0;
        }
    }
}

static bool perform_Mark_Pattern_Test(const tDevice*        device,
                                      uint32_t              deviceBufferSize,
                                      ptrPatternTestResults testResults,
                                      eCableTestMode        testMode,
                                      uint64_t              lba,
                                      uint64_t              lbaRange,
                                      fuaCmd                fuaCmdReq)
{
    bool     result = false;
    uint8_t* patternBuffer =
        C_CAST(uint8_t*,
               safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment)); // only send this to the drive
    if (patternBuffer != M_NULLPTR)
    {
        DECLARE_SEATIMER(patternTimer);
        fill_mark_pattern_in_buffer(patternBuffer, deviceBufferSize);
        start_Timer(&patternTimer);
        result = write_read_compare_pattern(device, patternBuffer, deviceBufferSize, lba, lbaRange, testResults,
                                            testMode, fuaCmdReq);
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_free_aligned(&patternBuffer);
    return result;
}

static bool fill_walking_test_pattern_in_buffer(uint8_t*  patternBuffer,
                                                uint32_t  deviceBufferSize,
                                                bool      walkingZeros,
                                                uint32_t* bitNumber,
                                                uint32_t* byteNumber)
{
    uint8_t bitToSet;
    safe_memset(patternBuffer, deviceBufferSize, walkingZeros ? 0xFF : 0x00, deviceBufferSize);
    if (*bitNumber > UINT32_C(7))
    {
        // this means we've shifted the bit through each bit of this byte, so offset to the next byte and start
        // again
        ++(*byteNumber);
        *bitNumber = UINT32_C(0);
        if (*byteNumber >= deviceBufferSize)
        {
            return false;
        }
    }
    bitToSet = M_STATIC_CAST(uint8_t, *bitNumber);
    if (walkingZeros)
    {
        patternBuffer[*byteNumber] = clear_uint8_bit(patternBuffer[*byteNumber], bitToSet);
    }
    else
    {
        patternBuffer[*byteNumber] = set_uint8_bit(patternBuffer[*byteNumber], bitToSet);
    }
    return true;
}

// Function for Walking 1's/0's test
static void perform_Walking_Test(const tDevice*        device,
                                 bool                  walkingZeros,
                                 uint32_t              deviceBufferSize,
                                 ptrPatternTestResults testResults,
                                 eCableTestMode        testMode,
                                 uint64_t              lba,
                                 uint64_t              lbaRange,
                                 fuaCmd                fuaCmdReq)
{
    uint8_t* patternBuffer = M_REINTERPRET_CAST(
        uint8_t*,
        safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment)); // only send this to the drive
    if (patternBuffer != M_NULLPTR)
    {
        uint32_t lbasPerBuffer = get_LBAs_Per_BufferSize(device, deviceBufferSize);
        uint64_t bytemax       = (deviceBufferSize / lbasPerBuffer);
        DECLARE_SEATIMER(patternTimer);
        start_Timer(&patternTimer);
        for (uint32_t bitNumber = UINT32_C(0), byteNumber = UINT32_C(0); byteNumber < bytemax; ++bitNumber)
        {
            // TODO: Change deviceBufferSize to bytemax being passed in since we memcpy this which may slightly improve
            // performance.
            if (!fill_walking_test_pattern_in_buffer(patternBuffer, bytemax, walkingZeros, &bitNumber, &byteNumber) ||
                byteNumber >= bytemax)
            {
                break; // finished all bits in the buffer
            }
            for (uint32_t lbaCopyIter = UINT32_C(1); lbaCopyIter < lbasPerBuffer; ++lbaCopyIter)
            {
                safe_memcpy(&patternBuffer[device->drive_info.deviceBlockSize * (lbaCopyIter)],
                            deviceBufferSize - (device->drive_info.deviceBlockSize * (lbaCopyIter)), &patternBuffer[0],
                            M_Min(device->drive_info.deviceBlockSize,
                                  deviceBufferSize - (device->drive_info.deviceBlockSize * (lbaCopyIter))));
            }

            if (!write_read_compare_pattern(device, patternBuffer, deviceBufferSize, lba, lbaRange, testResults,
                                            testMode, fuaCmdReq))
            {
                break;
            }
        }
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_free_aligned(&patternBuffer);
}
// Function for random data pattern test
static bool perform_Random_Pattern_Test(const tDevice*        device,
                                        uint32_t              deviceBufferSize,
                                        ptrPatternTestResults testResults,
                                        eCableTestMode        testMode,
                                        uint64_t              lba,
                                        uint64_t              lbaRange,
                                        fuaCmd                fuaCmdReq)
{
    bool     result = false;
    uint8_t* patternBuffer =
        C_CAST(uint8_t*,
               safe_malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment)); // only send this to the drive
    if (patternBuffer != M_NULLPTR)
    {
        DECLARE_SEATIMER(patternTimer);
        start_Timer(&patternTimer);
        fill_Random_Pattern_In_Buffer(patternBuffer, deviceBufferSize); // set a new random pattern each time
        result = write_read_compare_pattern(device, patternBuffer, deviceBufferSize, lba, lbaRange, testResults,
                                            testMode, fuaCmdReq);
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_free_aligned(&patternBuffer);
    return result;
}

// SATA Phy event counters: CRC = definitely bad
//                          R_ERR = multiple possible causes from bad connection to bad cable. Recommend redoing the
//                          connection or replacing cable.
// SATA Device statistics: CRC = definitely bad
//                         ASR events = bad cable as well. (asynchronous signal recovery)
// SAS SPL error counters: Invalid Dword = definitely bad
//                         Running disparity or loss of sync = multiple possible causes from bad connection to bad
//                         cable. Recommend redoing the connection or replacing cable.
// Slower interface speed = longer test time to get a confident result.

// main function for the whole test.
static eReturnValues perform_Pattern_Test(const tDevice*      device,
                                          ptrCableTestResults testResults,
                                          eCableTestMode      testMode,
                                          uint64_t            startingLBA,
                                          uint64_t            lbaRange,
                                          fuaCmd              fuaCmdReq)
{
    eReturnValues ret = SUCCESS;
    DISABLE_NONNULL_COMPARE
    if (testResults == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (are_Buffer_Commands_Available(device))
    {
        uint8_t  offsetPO2  = UINT8_C(0); // This shouldn't actually be needed...but I have it here in case I do
        uint32_t bufferSize = UINT32_C(0);
        switch (testMode)
        {
        case CABLE_TEST_MODE_BUFFER_CMDS:
            get_Buffer_Size(device, &bufferSize, &offsetPO2);
            break;
        case CABLE_TEST_MODE_READ_WRITE_CMDS:
            bufferSize = get_Sector_Count_For_Read_Write(device) * device->drive_info.deviceBlockSize;
            break;
        }
        if (bufferSize > UINT32_C(0))
        {
            DECLARE_SEATIMER(totalTestingTime);
            // drive supports the read/write buffer commands we need and we know what size the buffer is we can test
            // with. now we need to begin testing.
            M_INITIALIZE_STRUCTURE(testResults, sizeof(cableTestResults));
            // first, lets do some simple data patterns (0's, F's, 5's, A's)
            start_Timer(&totalTestingTime);
            puts("Zeros test");
            for (uint8_t count = UINT8_C(0); count < ALL_0_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x00000000), bufferSize, &testResults->zerosTest[count],
                                          testMode, startingLBA, lbaRange, fuaCmdReq);
            }
            puts("F's test");
            for (uint8_t count = UINT8_C(0); count < ALL_F_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0xFFFFFFFF), bufferSize, &testResults->fTest[count],
                                          testMode, startingLBA, lbaRange, fuaCmdReq);
            }
            puts("5's test");
            for (uint8_t count = UINT8_C(0); count < ALL_5_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x55555555), bufferSize, &testResults->fivesTest[count],
                                          testMode, startingLBA, lbaRange, fuaCmdReq);
            }
            puts("A's test");
            for (uint8_t count = UINT8_C(0); count < ALL_A_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0xAAAAAAAA), bufferSize, &testResults->aTest[count],
                                          testMode, startingLBA, lbaRange, fuaCmdReq);
            }
            // checker board - byte
            puts("Checkerboard (Byte) test");
            for (uint8_t count = UINT8_C(0); count < CHECKER_BOARD_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x55AA55AA), bufferSize,
                                          &testResults->checkerBoardByte[count], testMode, startingLBA, lbaRange,
                                          fuaCmdReq);
            }
            // checker board - word
            puts("Checkerboard (Word) test");
            for (uint8_t count = UINT8_C(0); count < CHECKER_BOARD_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x5555AAAA), bufferSize,
                                          &testResults->checkerBoardWord[count], testMode, startingLBA, lbaRange,
                                          fuaCmdReq);
            }
            // mark
            puts("Mark test");
            for (uint8_t count = UINT8_C(0); count < MARK_TEST_COUNT; ++count)
            {
                perform_Mark_Pattern_Test(device, bufferSize, &testResults->mark[count], testMode, startingLBA,
                                          lbaRange, fuaCmdReq);
            }

            // Row Boat
            puts("Rowboat 00-FF-55-AA test");
            for (uint8_t count = UINT8_C(0); count < ZERO_F_5_A_TEST_COUNT; ++count)
            {
                rowBoatPatternSequence myRowBoat = {ROW_BOAT_PATTERN_00, ROW_BOAT_PATTERN_FF, ROW_BOAT_PATTERN_55,
                                                    ROW_BOAT_PATTERN_AA};
                perform_RowBoat_Pattern_Test(device, myRowBoat, bufferSize, &testResults->zeroF5ATest[count], testMode,
                                             startingLBA, lbaRange, fuaCmdReq);
            }
            puts("Rowboat 1 test");
            for (uint8_t count = UINT8_C(0); count < ROW_BOAT_TEST_COUNT; ++count)
            {
                perform_RowBoat_Pattern_Test(device, seq1, bufferSize, &testResults->rowBoat1[count], testMode,
                                             startingLBA, lbaRange, fuaCmdReq);
            }
            puts("Rowboat 2 test");
            for (uint8_t count = UINT8_C(0); count < ROW_BOAT_TEST_COUNT; ++count)
            {
                perform_RowBoat_Pattern_Test(device, seq2, bufferSize, &testResults->rowBoat2[count], testMode,
                                             startingLBA, lbaRange, fuaCmdReq);
            }
            puts("Rowboat 3 test");
            for (uint8_t count = UINT8_C(0); count < ROW_BOAT_TEST_COUNT; ++count)
            {
                perform_RowBoat_Pattern_Test(device, seq3, bufferSize, &testResults->rowBoat3[count], testMode,
                                             startingLBA, lbaRange, fuaCmdReq);
            }
            puts("Rowboat 4 test");
            for (uint8_t count = UINT8_C(0); count < ROW_BOAT_TEST_COUNT; ++count)
            {
                perform_RowBoat_Pattern_Test(device, seq4, bufferSize, &testResults->rowBoat4[count], testMode,
                                             startingLBA, lbaRange, fuaCmdReq);
            }
            uint64_t walkingTestRange = lbaRange;
            if (testMode == CABLE_TEST_MODE_BUFFER_CMDS)
            {
                walkingTestRange = 1;
            }
            // now walking 1's
            puts("Walking 1's test");
            for (uint8_t count = UINT8_C(0); count < WALKING_1_TEST_COUNT; ++count)
            {
                perform_Walking_Test(device, false, bufferSize, &testResults->walking1sTest[count], testMode,
                                     startingLBA, walkingTestRange, fuaCmdReq);
            }
            // walking 0's
            puts("Walking 0's test");
            for (uint8_t count = UINT8_C(0); count < WALKING_0_TEST_COUNT; ++count)
            {
                perform_Walking_Test(device, true, bufferSize, &testResults->walking0sTest[count], testMode,
                                     startingLBA, walkingTestRange, fuaCmdReq);
            }
            // random data patterns
            puts("Random test");
            for (uint8_t count = UINT8_C(0); count < RANDOM_TEST_COUNT; ++count)
            {
                perform_Random_Pattern_Test(device, bufferSize, &testResults->randomTest[count], testMode, startingLBA,
                                            lbaRange, fuaCmdReq);
            }
            stop_Timer(&totalTestingTime);
            testResults->totalTestTimeNS = get_Nano_Seconds(totalTestingTime);
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

eReturnValues perform_Cable_Test(const tDevice* device, ptrCableTestResults testResults)
{
    fuaCmd notNeeded = {false, false};
    return perform_Pattern_Test(device, testResults, CABLE_TEST_MODE_BUFFER_CMDS, RESERVED, 1, notNeeded);
}

eReturnValues perform_Write_Read_Compare_Test(const tDevice*      device,
                                              ptrCableTestResults testResults,
                                              uint64_t            startingLBA,
                                              uint64_t            range,
                                              fuaCmd              fuaCmdReq)
{
    eReturnValues ret = SUCCESS;
    ret = perform_Pattern_Test(device, testResults, CABLE_TEST_MODE_READ_WRITE_CMDS, startingLBA, range, fuaCmdReq);
    return ret;
}

static void print_Individual_Test_Results(const char* testname, ptrPatternTestResults results, size_t maxcount)
{
    if (results != M_NULLPTR)
    {
        print_str(testname);
        print_str("\n");
        uint64_t combinedRunsTime = UINT64_C(0);
        for (uint8_t count = UINT8_C(0); count < maxcount; ++count)
        {
            printf("    Run %" PRIu8 ":\n", count + UINT8_C(1));
            printf("        Total commands sent: %" PRIu32 "\n", results[count].totalCommandsSent);
            printf("        Number of command CRC errors: %" PRIu32 "\n", results[count].totalCommandCRCErrors);
            printf("        Number of command timeouts: %" PRIu32 "\n", results[count].totalCommandTimeouts);
            printf("        Number of buffer comparisons: %" PRIu32 "\n", results[count].totalBufferComparisons);
            printf("        Number of buffer miscompares: %" PRIu32 "\n", results[count].totalBufferMiscompares);
            print_str("        Test time ");
            print_Time(results[count].totalTimeNS);
            print_str("\n");
            combinedRunsTime += results[count].totalTimeNS;
        }
        print_str("Combined Runs Test time ");
        print_Time(combinedRunsTime);
        print_str("\n");
    }
}

void print_Cable_Test_Results(cableTestResults testResults)
{
    print_str("Test Results:\n");
    print_str("=============\n");
    print_str("Total test time ");
    print_Time(testResults.totalTestTimeNS);
    print_str("\n");
    print_Individual_Test_Results("00h Test Pattern", testResults.zerosTest, ALL_0_TEST_COUNT);
    print_Individual_Test_Results("FFh Test Pattern", testResults.fTest, ALL_F_TEST_COUNT);
    print_Individual_Test_Results("55h Test Pattern", testResults.fivesTest, ALL_5_TEST_COUNT);
    print_Individual_Test_Results("AAh Test Pattern", testResults.aTest, ALL_A_TEST_COUNT);
    print_Individual_Test_Results("Checkerboard (Byte) Test Pattern", testResults.checkerBoardByte,
                                  CHECKER_BOARD_TEST_COUNT);
    print_Individual_Test_Results("Checkerboard (Word) Test Pattern", testResults.checkerBoardWord,
                                  CHECKER_BOARD_TEST_COUNT);
    print_Individual_Test_Results("Mark Test Pattern", testResults.mark, MARK_TEST_COUNT);
    print_Individual_Test_Results("Rowboat 00FF55AAh Test Pattern", testResults.zeroF5ATest, ZERO_F_5_A_TEST_COUNT);
    print_Individual_Test_Results("Rowboat 1 Test Pattern", testResults.rowBoat1, ROW_BOAT_TEST_COUNT);
    print_Individual_Test_Results("Rowboat 2 Test Pattern", testResults.rowBoat2, ROW_BOAT_TEST_COUNT);
    print_Individual_Test_Results("Rowboat 3 Test Pattern", testResults.rowBoat3, ROW_BOAT_TEST_COUNT);
    print_Individual_Test_Results("Rowboat 4 Test Pattern", testResults.rowBoat4, ROW_BOAT_TEST_COUNT);
    print_Individual_Test_Results("Walking 1's Test Pattern", testResults.walking1sTest, WALKING_1_TEST_COUNT);
    print_Individual_Test_Results("Walking 0's Test Pattern", testResults.walking0sTest, WALKING_0_TEST_COUNT);
    print_Individual_Test_Results("Random Test Pattern", testResults.randomTest, RANDOM_TEST_COUNT);
}
