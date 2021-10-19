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
// \file buffer_test.c
// \brief This file defines the function calls for performing buffer/cabling tests

#include "buffer_test.h"


static bool are_Buffer_Commands_Available(tDevice *device)
{
    bool supported = false;
    //Check if read/write buffer commands are supported on SATA and SAS
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if ((device->drive_info.IdentifyData.ata.Word082 != 0xFFFF && device->drive_info.IdentifyData.ata.Word082 != 0 //make sure word 82 is valid
            &&
            device->drive_info.IdentifyData.ata.Word082 & BIT13 && device->drive_info.IdentifyData.ata.Word082 & BIT12)//check support bits
            ||
            (device->drive_info.IdentifyData.ata.Word085 != 0xFFFF && device->drive_info.IdentifyData.ata.Word085 != 0 //make sure word 85 is valid
            &&
            device->drive_info.IdentifyData.ata.Word085 & BIT13 && device->drive_info.IdentifyData.ata.Word085 & BIT12//check support bits
            )
           )
        {
            //PIO commands
            supported = true;
        }
        if (device->drive_info.IdentifyData.ata.Word069 & BIT11 && device->drive_info.IdentifyData.ata.Word069 & BIT10)
        {
            //DMA commands
            supported = true;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //SCSI 2 + should support this.
        //SCSI 1 probably won't...but this is so old it may not be a problem
        //Only asking about read buffer command, since write buffer will likely be implemented for at least FWDL, so if this is supported, the equivalent write buffer command should also be supported
        bool driveReportedSupport = false;
        uint8_t supportedCommandData[16] = { 0 };
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, READ_BUFFER_CMD, 0x02, 14, supportedCommandData))//trying w/ service action (newer SPC spec allows this)
        {
            driveReportedSupport = true;
        }
        else if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, READ_BUFFER_CMD, 0, 14, supportedCommandData))//older SPC3
        {
            //NOTE: we are just going to assume that if this gives us success, that this mode is supported, even though we don't know for sure...it SHOULD be supported if we got that this command is supported, which is good enough
            driveReportedSupport = true;
        }
        else if (SUCCESS == scsi_Inquiry(device, supportedCommandData, 16, READ_BUFFER_CMD, false, true))//really old SPC spec used inquiry to get support information
        {
            driveReportedSupport = true;
        }
        if (driveReportedSupport)
        {
            uint8_t support = M_GETBITRANGE(supportedCommandData[1], 2, 0);
            switch (support)
            {
            case 3://supported in accordance with SCSI specification
                supported = true;
                break;
            default://not supported, unknown support, or vendor unique/non-standard support
                supported = false;
                break;
            }
        }
        else
        {
            //this means the command to ask about support didn't work, so we're just going to try asking the size of the buffer and if that works, it is supported
            memset(supportedCommandData, 0, 16);
            if (SUCCESS == scsi_Read_Buffer(device, 0x03, 0, 0, 4, supportedCommandData))
            {
                //TODO: check the buffer capacity for non-zero value?
                supported = true;
            }
        }
    }
    return supported;
}

static int get_Buffer_Size(tDevice *device, uint32_t *bufferSize, uint8_t *offsetBoundary)
{
    int ret = SUCCESS;
    if (!bufferSize || !offsetBoundary)
    {
        return BAD_PARAMETER;
    }
    *bufferSize = LEGACY_DRIVE_SEC_SIZE;//default to this size. Change this only if the drive reports a different size
    *offsetBoundary = 0x09;//default to this size. Change this only if the drive reports a different size
    //get the size of the buffer for the drive.
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t bufferSizeData[4] = { 0 };
        if (SUCCESS == scsi_Read_Buffer(device, 0x03, 0, 0, 4, bufferSizeData))
        {
            *offsetBoundary = bufferSizeData[0];//not sure if this is actually needed - TJE
            *bufferSize = M_BytesTo4ByteValue(0, bufferSizeData[1], bufferSizeData[2], bufferSizeData[3]);
        }
        else
        {
            ret = FAILURE;//this shouldn't happen...
        }
    }
    return ret;
}

int send_Read_Buffer_Command(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //return ata_Read_Buffer(device, ptrData, device->drive_info.ata_Options.readBufferDMASupported);
        //Switching to this new function since it will automatically try DMA mode if supported by the drive.
        //If the controller or driver don't like issuing DMA mode, this will detect it and retry the command with PIO mode.
        return send_ATA_Read_Buffer_Cmd(device, ptrData);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Read_Buffer(device, 0x02, 0, 0, dataSize, ptrData);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

static int send_Write_Buffer_Command(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //return ata_Write_Buffer(device, ptrData, device->drive_info.ata_Options.writeBufferDMASupported);
        //Switching to this new function since it will automatically try DMA mode if supported by the drive.
        //If the controller or driver don't like issuing DMA mode, this will detect it and retry the command with PIO mode.
        return send_ATA_Write_Buffer_Cmd(device, ptrData);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Write_Buffer(device, 0x02, 0, 0, 0, dataSize, ptrData, false, false, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

bool was_There_A_CRC_Error_On_Last_Command(tDevice *device)
{
    bool crc = false;
    bool checkSenseData = false;
    uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.lastCommandRTFRs.status & ATA_STATUS_BIT_ERROR)//error bit set
        {
            if (device->drive_info.lastCommandRTFRs.error & ATA_ERROR_BIT_INTERFACE_CRC)
            {
                crc = true;
            }
        }
        if (device->drive_info.ataSenseData.validData && !crc)
        {
            checkSenseData = true;
            senseKey = device->drive_info.ataSenseData.senseKey;
            asc = device->drive_info.ataSenseData.additionalSenseCode;
            ascq = device->drive_info.ataSenseData.additionalSenseCodeQualifier;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        checkSenseData = true;
        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
    }
    if (checkSenseData)
    {
        if (senseKey == SENSE_KEY_ABORTED_COMMAND)//INFORMATION UNIT iuCRC ERROR DETECTED
        {
            switch (asc)
            {
            case 0x08:
                if (ascq == 0x03)//LOGICAL UNIT COMMUNICATION CRC ERROR (ULTRA-DMA/32)
                {
                    crc = true;
                }
                break;
            case 0x10:
                if (ascq == 0x10)//ID CRC OR ECC ERROR
                {
                    crc = true;
                }
                break;
            case 0x11:
                if (ascq == 0x0D)//DE-COMPRESSION CRC ERROR
                {
                    crc = true;
                }
                break;
            case 0x47:
                switch (ascq)
                {
                    //TODO: other asc 0x47 cases may fit CRC error
                case 0x01://DATA PHASE CRC ERROR DETECTED
                case 0x03://INFORMATION UNIT iuCRC ERROR DETECTED - SAT will translate a CRC error into this! Definitely need this one. Less sure about the others...-TJE
                case 0x05://PROTOCOL SERVICE CRC ERROR
                    crc = true;
                    break;
                default:
                    break;
                }
                break;
            case 0x4B:
                if (ascq == 0x12)//PCIE ECRC CHECK FAILED
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

//Function for simple byte pattern tests. take counter for number of times to try it?
void perform_Byte_Pattern_Test(tDevice *device, uint32_t pattern, uint32_t deviceBufferSize, ptrPatternTestResults testResults)
{
    uint32_t numberOfTimesToTest = 5;
    uint8_t *patternBuffer = C_CAST(uint8_t*, malloc(deviceBufferSize));//only send this to the drive
    uint8_t *returnBuffer = C_CAST(uint8_t*, malloc(deviceBufferSize));//only receive this from the drive
    if (patternBuffer && returnBuffer)
    {
        fill_Pattern_Buffer_Into_Another_Buffer((uint8_t*)&pattern, 4, patternBuffer, deviceBufferSize);//sets the pattern to write into memory
        seatimer_t patternTimer;
        memset(&patternTimer, 0, sizeof(seatimer_t));
        start_Timer(&patternTimer);
        for (uint32_t counter = 0; counter < numberOfTimesToTest; ++counter)
        {
            bool breakFromLoop = false;//this will be set to true when we need to exit the loop for one reason or another
            int wbResult = send_Write_Buffer_Command(device, patternBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (wbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            //now read back the pattern
            memset(returnBuffer, 0, deviceBufferSize);
            int rbResult = send_Read_Buffer_Command(device, returnBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (rbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            ++(testResults->totalBufferComparisons);
            //first check if the pattern matches or not
            if (memcmp(patternBuffer, returnBuffer, deviceBufferSize))
            {
                ++(testResults->totalBufferMiscompares);
            }
        }
        stop_Timer(&patternTimer);
        testResults->totalTimeNS = get_Nano_Seconds(patternTimer);
    }
    safe_Free(patternBuffer)
    safe_Free(returnBuffer)
}

//Function for Walking 1's/0's test
void perform_Walking_Test(tDevice *device, bool walkingZeros, uint32_t deviceBufferSize, ptrPatternTestResults testResults)
{
    uint8_t *patternBuffer = C_CAST(uint8_t*, calloc_aligned(deviceBufferSize, sizeof(uint8_t), device->os_info.minimumAlignment));//only send this to the drive
    uint8_t *returnBuffer = C_CAST(uint8_t*, malloc_aligned(deviceBufferSize, device->os_info.minimumAlignment));//only receive this from the drive
    if (patternBuffer && returnBuffer)
    {
        for (uint32_t bitNumber = 0, byteNumber = 0; byteNumber < deviceBufferSize; ++bitNumber)
        {
            bool breakFromLoop = false;
            //set the pattern
            if (walkingZeros)
            {
                memset(patternBuffer, 0xFF, deviceBufferSize);
            }
            else
            {
                memset(patternBuffer, 0, deviceBufferSize);
            }
            if (bitNumber > 7)
            {
                //this means we've shifted the bit through each bit of this byte, so offset to the next byte and start again
                ++byteNumber;
                bitNumber = 0;
                if (byteNumber >= deviceBufferSize)
                {
                    break;
                }
            }
            if (walkingZeros)
            {
                patternBuffer[byteNumber] ^= M_BitN(bitNumber);//exclusive or should turn this bit to a zero
            }
            else
            {
                patternBuffer[byteNumber] |= M_BitN(bitNumber);
            }
            int wbResult = send_Write_Buffer_Command(device, patternBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (wbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            //now read back the pattern
            memset(returnBuffer, 0, deviceBufferSize);
            int rbResult = send_Read_Buffer_Command(device, returnBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (rbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            ++(testResults->totalBufferComparisons);
            //first check if the pattern matches or not
            if (memcmp(patternBuffer, returnBuffer, deviceBufferSize))
            {
                ++(testResults->totalBufferMiscompares);
            }
        }
    }
    safe_Free_aligned(patternBuffer)
    safe_Free_aligned(returnBuffer)
}
//Function for random data pattern test
void perform_Random_Pattern_Test(tDevice *device, uint32_t deviceBufferSize, ptrPatternTestResults testResults)
{
    uint32_t numberOfTimesToTest = 10;
    uint8_t *patternBuffer = C_CAST(uint8_t*, malloc(deviceBufferSize));//only send this to the drive
    uint8_t *returnBuffer = C_CAST(uint8_t*, malloc(deviceBufferSize));//only receive this from the drive
    if (patternBuffer && returnBuffer)
    {
        for (uint32_t counter = 0; counter < numberOfTimesToTest; ++counter)
        {
            bool breakFromLoop = false;
            fill_Random_Pattern_In_Buffer(patternBuffer, deviceBufferSize);//set a new random pattern each time
            int wbResult = send_Write_Buffer_Command(device, patternBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (wbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            //now read back the pattern
            memset(returnBuffer, 0, deviceBufferSize);
            int rbResult = send_Read_Buffer_Command(device, returnBuffer, deviceBufferSize);
            ++(testResults->totalCommandsSent);
            switch (rbResult)
            {
            case OS_PASSTHROUGH_FAILURE:
            case NOT_SUPPORTED:
                breakFromLoop = true;
                break;
            case COMMAND_TIMEOUT:
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
                continue;//continue loop since this will miscompare no matter what on the read buffer command
            }
            if (breakFromLoop)
            {
                break;
            }
            ++(testResults->totalBufferComparisons);
            //first check if the pattern matches or not
            if (memcmp(patternBuffer, returnBuffer, deviceBufferSize))
            {
                ++(testResults->totalBufferMiscompares);
            }
        }
    }
    safe_Free(patternBuffer)
    safe_Free(returnBuffer)
}

//master function for the whole test.
int perform_Cable_Test(tDevice *device, ptrCableTestResults testResults)
{
    int ret = SUCCESS;
    if (!testResults)
    {
        return BAD_PARAMETER;
    }
    if (are_Buffer_Commands_Available(device))
    {
        uint8_t offsetPO2 = 0;//This shouldn't actually be needed...but I have it here in case I do
        uint32_t bufferSize = 0;
        if (SUCCESS == get_Buffer_Size(device, &bufferSize, &offsetPO2) && bufferSize > 0)
        {
            seatimer_t totalTestingTime;
            memset(&totalTestingTime, 0, sizeof(seatimer_t));
            //drive supports the read/write buffer commands we need and we know what size the buffer is we can test with.
            //now we need to begin testing.
            memset(testResults, 0, sizeof(cableTestResults));
            //first, lets do some simple data patterns (0's, F's, 5's, A's)
            start_Timer(&totalTestingTime);
            for (uint8_t count = 0; count < ALL_0_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x00000000), bufferSize, &testResults->zerosTest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
            }
            for (uint8_t count = 0; count < ALL_F_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0xFFFFFFFF), bufferSize, &testResults->fTest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
            }
            for (uint8_t count = 0; count < ALL_5_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x55555555), bufferSize, &testResults->fivesTest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
            }
            for (uint8_t count = 0; count < ALL_A_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0xAAAAAAAA), bufferSize, &testResults->aTest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
            }
            for (uint8_t count = 0; count < ZERO_F_5_A_TEST_COUNT; ++count)
            {
                perform_Byte_Pattern_Test(device, UINT32_C(0x00FF55AA), bufferSize, &testResults->zeroF5ATest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
            }
            //now walking 1's
            for (uint8_t count = 0; count < WALKING_1_TEST_COUNT; ++count)
            {
                perform_Walking_Test(device, false, bufferSize, &testResults->walking1sTest[count]);//arbitraty number 5 was chose since it sounded good for trying this test
            }
            //walking 0's
            for (uint8_t count = 0; count < WALKING_0_TEST_COUNT; ++count)
            {
                perform_Walking_Test(device, true, bufferSize, &testResults->walking0sTest[count]);//arbitraty number 5 was chose since it sounded good for trying this test
            }
            //random data patterns
            for (uint8_t count = 0; count < RANDOM_TEST_COUNT; ++count)
            {
                perform_Random_Pattern_Test(device, bufferSize, &testResults->randomTest[count]);//arbitrary number 10 was chosen since it sounded good for number of times to try this pattern
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

void print_Cable_Test_Results(cableTestResults testResults)
{
    printf("Test Results:\n");
    printf("=============\n");
    printf("Total test time: ");
    print_Command_Time(testResults.totalTestTimeNS);
    printf("\n");
    printf("00h Test Pattern:\n");
    for (uint8_t count = 0; count < ALL_0_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + UINT8_C(1));
        printf("        Total commands sent: %" PRIu32 "\n", testResults.zerosTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.zerosTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.zerosTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.zerosTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.zerosTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.zerosTest[count].totalTimeNS);
        printf("\n");
    }
    printf("FFh Test Pattern:\n");
    for (uint8_t count = 0; count < ALL_F_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.fTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.fTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.fTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.fTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.fTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.fTest[count].totalTimeNS);
        printf("\n");
    }
    printf("55h Test Pattern:\n");
    for (uint8_t count = 0; count < ALL_5_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.fivesTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.fivesTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.fivesTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.fivesTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.fivesTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.fivesTest[count].totalTimeNS);
        printf("\n");
    }
    printf("AAh Test Pattern:\n");
    for (uint8_t count = 0; count < ALL_A_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.aTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.aTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.aTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.aTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.aTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.aTest[count].totalTimeNS);
        printf("\n");
    }
    printf("00FF55AAh Test Pattern:\n");
    for (uint8_t count = 0; count < ZERO_F_5_A_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.zeroF5ATest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.zeroF5ATest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.zeroF5ATest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.zeroF5ATest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.zeroF5ATest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.zeroF5ATest[count].totalTimeNS);
        printf("\n");
    }
    printf("Walking 1's Test:\n");
    for (uint8_t count = 0; count < WALKING_1_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.walking1sTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.walking1sTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.walking1sTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.walking1sTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.walking1sTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.walking1sTest[count].totalTimeNS);
        printf("\n");
    }
    printf("Walking 0's Test:\n");
    for (uint8_t count = 0; count < WALKING_0_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.walking0sTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.walking0sTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.walking0sTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.walking0sTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.walking0sTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.walking0sTest[count].totalTimeNS);
        printf("\n");
    }
    printf("Random Pattern Test:\n");
    for (uint8_t count = 0; count < RANDOM_TEST_COUNT; ++count)
    {
        printf("    Run %" PRIu8 ":\n", count + 1);
        printf("        Total commands sent: %" PRIu32 "\n", testResults.randomTest[count].totalCommandsSent);
        printf("        Number of command CRC errors: %" PRIu32 "\n", testResults.randomTest[count].totalCommandCRCErrors);
        printf("        Number of command timeouts: %" PRIu32 "\n", testResults.randomTest[count].totalCommandTimeouts);
        printf("        Number of buffer comparisons: %" PRIu32 "\n", testResults.randomTest[count].totalBufferComparisons);
        printf("        Number of buffer miscompares: %" PRIu32 "\n", testResults.randomTest[count].totalBufferMiscompares);
        printf("        Test time: ");
        print_Command_Time(testResults.randomTest[count].totalTimeNS);
        printf("\n");
    }
}
