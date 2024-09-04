// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file logs.h
// \brief This file defines the functions for pulling logs from SCSI and ATA drives

#pragma once

#include "operations_Common.h"
#include "common_types.h"
#include "code_attributes.h"
#include "secure_file.h"

#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

    typedef enum _eLogPullMode
    {
        PULL_LOG_RAW_MODE,          // Dump it to stdout. 
        PULL_LOG_BIN_FILE_MODE,     // Create a binary file 
        PULL_LOG_ANALYZE_MODE,      // Humanize the log
        PULL_LOG_PIPE_MODE,         // Dump it to stdout and send the result to openSeaChest_LogParser
    } eLogPullMode;

    #define FARM_SUBLOGPAGE_LEN             16384
    #define TOTAL_CONSTITUENT_PAGES         32

    OPENSEA_OPERATIONS_API const char* get_Drive_ID_For_Logfile_Name(tDevice *device);

    //Meant to be a little simpler to call when you don't want to calculate a bunch of lengths for the function above using device info.
    //NOTE: This function does not return the name used as that is part of the secureFileInfo -TJE
    OPENSEA_OPERATIONS_API eReturnValues create_And_Open_Secure_Log_File_Dev_EZ(tDevice *device,
                                                secureFileInfo **file, /*required*/
                                                eLogFileNamingConvention logFileNamingConvention, /*required*/
                                                const char *logPath, //optional /*requested path to output to. Will be checked for security. If NULL, current directory will be used*/
                                                const char *logName, //optional /*name of the log file from the drive, FARM, DST, etc*/
                                                const char *logExt //optional /*extension for the log file. If NULL, set to .bin*/
    );
    
    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Log_Size(tDevice *device, uint8_t logAddress, uint32_t *logFileSize, bool gpl, bool smart)
    //
    //! \brief   Description: This function will check for the size of an ATA log as reported in the GPL or SMART directory. 
    //           The size returned is in bytes. 
    //           If a log is not supported, this function will return NOT_SUPPORTED and logfilesize will be set to zero.
    //           TIP: Use this function to see if an ATA log is supported or not. 
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in] logAddress = GPL or SMART log address you wish to know the size of
    //!   \param[out] logFileSize = pointer to uint32_t that will hold the size of the log at the requested address in bytes. Will be zero when log is not supported;
    //!   \param[in] gpl = set to true to check for the log in the GPL directory
    //!   \param[in] smart = set to true to check for the log in the SMART directory
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, NOT_SUPPORTED = log is not supported by device, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_Log_Size(tDevice *device, uint8_t logAddress, uint32_t *logFileSize, bool gpl, bool smart);

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Log_Size(tDevice *device, uint8_t logPage, uint8_t logSubPage, uint32_t *logFileSize)
    //
    //! \brief   Description: This function will check for the size of an SCSI log and/or log subpage as reported by the device.
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in] logPage = page # of the log you want to know the size of
    //!   \param[in] logSubPage = subpage you want to know the size of. Set to 0 if no subpage is needed.
    //!   \param[out] logFileSize = pointer to uint32_t that will hold the size of the log at the requested address in bytes. Will be zero when log is not supported;
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, NOT_SUPPORTED = log is not supported by device, !SUCCESS means something went wrong
    //!                     If SUCCESS is returned AND the logFileSize is UINT16_MAX, then the page should be supported, but determining the length was not possible due to a likely firmware bug in the device.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Log_Size(tDevice *device, uint8_t logPage, uint8_t logSubPage, uint32_t *logFileSize);

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_VPD_Page_Size(tDevice *device, uint8_t vpdPage, uint32_t *vpdPageSize)
    //
    //! \brief   Description: This function will check for the size of an SCSI VPD page as reported by the device.
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in] vpdPage = VPD page number you wish to know the size of
    //!   \param[out] vpdPageSize = pointer to uint32_t that will hold the size of the log at the requested address in bytes. Will be zero when log is not supported;
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, NOT_SUPPORTED = log is not supported by device, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_VPD_Page_Size(tDevice *device, uint8_t vpdPage, uint32_t *vpdPageSize);

    //-----------------------------------------------------------------------------
    //
    //! get_ATA_Log( tDevice * device )
    //
    //! \brief   generic function to pull an ATA log and save it to a file
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  logAddress - the address of the log you wish to pull
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please! as this gets used for the filename
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  GPL - boolean flag specifying if you want to check the GPL directory for the log
    //!   \param[in]  SMART - boolean flag specifying if you want to check the SMART directory for the log
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer instead of file
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current working directory.
    //!   \param[in] featureRegister - this is the feature register for the command. default to zero for most commands.
    //! 
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_Log(tDevice *device, uint8_t logAddress, \
                                        const char *logName, const char *fileExtension, \
                                        bool GPL, bool SMART, bool toBuffer, \
                                        uint8_t *myBuf, uint32_t bufSize, \
                                        const char * const filePath, \
                                        uint32_t transferSizeBytes, uint16_t featureRegister);

    //-----------------------------------------------------------------------------
    //
    //! get_SCSI_Log
    //
    //! \brief   generic function to pull an SCSI log and save it to a file
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  logAddress - the address of the log you wish to pull
    //!   \param[in]  subpage - set this to something other than zero if looking for a specific subpage to a log
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) M_NULLPTR if no file output needed
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Log(tDevice *device, uint8_t logAddress, uint8_t subpage, \
                                        const char *logName, const char *fileExtension, bool toBuffer, \
                                        uint8_t *myBuf, uint32_t bufSize, \
                                        const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SCSI_VPD(tDevice *device, uint8_t pageCode, const char *logName, const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize)
    //
    //! \brief   generic function to pull an SCSI log and save it to a file
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  pageCode - the page code of the VPD you wish to pull
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please! as this gets used for the filename
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer instead of file
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_VPD(tDevice *device, uint8_t pageCode, const char *logName, \
                                        const char *fileExtension, bool toBuffer, uint8_t *myBuf, \
                                        uint32_t bufSize, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_Device_Statistics_Log( tDevice * device )
    //
    //! \brief   Pulls the Device Statistics log
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  filePath - string for the file output path. Set to M_NULLPTR for the current directory
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Device_Statistics_Log(tDevice * device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_EPC_log( tDevice * device )
    //
    //! \brief   Pulls the Power Conditions log/VPD page
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_EPC_log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_Telemetry_Log()
    //
    //! \brief   Description:  this function will pull Internal Status logs from an ATA or SCSI device or the host or controller telemetry log on NVMe devices.
    //!                        These logs are all referred to as telemetry to keep things simple and a common name across interfaces
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param currentOrSaved - boolean flag to switch between pulling the current log or the saved log (current is currently the only log supported so set this to true). On NVMe current = host, saved = controller
    //!   \param islDataSet - flag to pull the small, medium, or large dataset. 1 = small, 2 = medium, 3 = large, 4 = extra large (SAS only)
    //!   \param saveToFile - boolean flag to tell it to save to a file with an auto generated name (naming is based off of serial number and current date and time)
    //!   \param ptrData - pointer to a data buffer. This MUST be non-M_NULLPTR when saveToFile = false
    //!   \param dataSize - size of the buffer that ptrData points to. This should be at least 256K for the small data set.
    //!   \param [in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current working directory.
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_Telemetry_Log(tDevice *device, \
                                                bool currentOrSaved, \
                                                uint8_t islDataSet, \
                                                bool saveToFile, \
                                                uint8_t* ptrData, \
                                                uint32_t dataSize, \
                                                const char * const filePath, \
                                                uint32_t transferSizeBytes);

    //-----------------------------------------------------------------------------
    //
    //! get_Pending_Defect_List( tDevice * device )
    //
    //! \brief   Pulls the ACS4/SBC4 Pending Defects log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Pending_Defect_List(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SMART_Extended_Comprehensive_Error_Log( tDevice * device )
    //
    //! \brief   Pulls the SMART Extended Comprehensive Error Log. ATA Only
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SMART_Extended_Comprehensive_Error_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_ATA_DST_Log( tDevice * device )
    //
    //! \brief   Pulls the DST log from an ATA drive. (SMART or GPL log)
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in] extLog - set to true to read the GPL log, false for the SMART log. Recommended you use device->drive_info.ata_Options.generalPurposeLoggingSupported for this value.
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_DST_Log(tDevice *device, bool extLog, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_DST_Log( tDevice * device )
    //
    //! \brief   Pulls the DST log from an ATA or SCSI device.
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_DST_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_Identify_Device_Data_Log( tDevice * device )
    //
    //! \brief   Pulls the ATA Identify Device Data Log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Identify_Device_Data_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SATA_Phy_Event_Counters_Log( tDevice * device )
    //
    //! \brief   Pulls the SATA Phy Event Counters Log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SATA_Phy_Event_Counters_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_SCSI_G_List(tDevice *device)
    //
    //! \brief   Description: This function pulls the G-List from SAS drives using the read defect data 12 command. 
    //                        Pulled with log block address descriptors (LBAs)
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_SCSI_G_List(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_SCSI_Informational_Exceptions_Log(tDevice *device)
    //
    //! \brief   Description: This function pulls the Informational Exceptions log from a SCSI drive.
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_SCSI_Informational_Exceptions_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_Logs()
    //
    //! \brief   Description:  This function prints the supported logs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param flags -  to filter the logs. 
    //!   
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Supported_Logs(tDevice *device, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_SCSI_Logs()
    //
    //! \brief   Description:  This function prints the supported SCSI logs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param flags -  to filter the logs. 
    //!   
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Supported_SCSI_Logs(tDevice *device, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_ATA_Logs()
    //
    //! \brief   Description:  This function prints the supported ATA logs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param flags -  to filter the logs. 
    //!   
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Supported_ATA_Logs(tDevice *device, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_NVMe_Logs()
    //
    //! \brief   Description:  This function prints the supported NVMe logs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param flags -  to filter the logs. 
    //!   
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Supported_NVMe_Logs(tDevice *device, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  pull_Generic_Log()
    //
    //! \brief   Description:  This function prints the supported NVMe logs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param logNum - log # to pull 
    //!   \param subpage - subpage of the log # to pull (for SCSI)
    //!   \param mode   - what mode to pull the log
    //!   \param filePath   - path for log file creation (if needed, otherwise set to M_NULLPTR)
    //!   \param transferSizeBytes - number of bytes to use with each read through a loop. For example: can be used to do 64k instead of a default amount
    //!   \param logLengthOverride - NVME only. Used to specify the total length of a log when known since the generic lookup may not get this correct or may not know the actual length
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_Generic_Log(tDevice *device, uint8_t logNum, uint8_t subpage, \
                                            eLogPullMode mode, const char * const filePath, \
                                            uint32_t transferSizeBytes, uint32_t logLengthOverride);

    OPENSEA_OPERATIONS_API eReturnValues pull_Generic_Error_History(tDevice *device, uint8_t bufferID, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_SCSI_Logs()
    //
    //! \brief   Description:  This function prints the supported SCSI error history buffer IDs to stdout
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param flags -  to filter the logs. 
    //!   
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Supported_SCSI_Error_History_Buffer_IDs(tDevice *device, uint64_t flags);

    OPENSEA_OPERATIONS_API bool is_SCSI_Read_Buffer_16_Supported(tDevice *device);//use for determining how to use this command to pull error history

    //Error history is formatted in vendor specific mannors.
    //Only exception is the current/saved internal status data. This should be pulled using the pull_Internal_Status_Log() function instead!
    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Error_History_Size(tDevice *device, uint8_t bufferID, uint32_t *errorHistorySize, bool createNewSnapshot)
    //
    //! \brief   Description: This function will check for the existance of an error history buffer ID and if found, return it's size
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in] bufferID = buffer ID of the error history data
    //!   \param[out] errorHistorySize = pointer to uint32_t that will hold the size of the error history at the requested address in bytes. Will be zero when log is not supported;
    //!   \param[in] createNewSnapshot = set to true to force generating new snap shot data. This changes from reading the directory with ID of 0 to 1.
    //!   \param[in] useReadBuffer16 = use the SPC5 read buffer 16 command to read the error history data
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, NOT_SUPPORTED = log is not supported by device, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Error_History_Size(tDevice *device, uint8_t bufferID, uint32_t *errorHistorySize, bool createNewSnapshot, bool useReadBuffer16);


    //-----------------------------------------------------------------------------
    //
    //! get_SCSI_Error_History(tDevice *device, uint8_t bufferID, const char *logName, bool createNewSnapshot,
    //! const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize,
    //! const char * const filePath);
    //
    //! \brief   generic function to pull an SCSI error history data and save it to a file
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  buffer ID - the buffer ID of the error history data
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) M_NULLPTR if no file output needed
    //!   \param[in]  createNewSnapshot - set to true to generate a new snapshot when reading the error history directory.
    //!   \param[in]  useReadBuffer16 - use the SPC5 read buffer 16 command
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
    //!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Error_History(tDevice *device, uint8_t bufferID, const char *logName, bool createNewSnapshot, bool useReadBuffer16, \
                                                    const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, \
                                                    const char * const filePath, uint32_t transferSizeBytes, char *fileNameUsed);
    
    OPENSEA_OPERATIONS_API eReturnValues pull_FARM_LogPage(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory, \
                                                uint16_t logPage, uint8_t logAddress, eLogPullMode mode);

    //-----------------------------------------------------------------------------
    //
    //  pull_FARM_Log(tDevice *device, const char * const filePath);
    //
    //! \brief   Description: This function pulls the Seagate FARM log from ATA drives
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current working dir.
    //!   \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored. 
    //!   \param[in] issueFactory = if set 0-4 issue the command with the factory feature. 
    //!                             FARM pull Factory subpages   
    //!                             0 - Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
    //!                             1 - Generate and report new FARM data and save to disc(~45ms)(SATA only)
    //!                             2 - Report previous FARM data from disc(~20ms)(SATA only)
    //!                             3 - Report FARM factory data from disc(~20ms)(SATA only)
    //!                             4 - factory subpage (SAS only)
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_FARM_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory, uint8_t logAddress, eLogPullMode mode);

    //-----------------------------------------------------------------------------
    //
    //  is_FARM_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate Current FARM log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_FARM_Log_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Factory_FARM_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate FARM Factory log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Factory_FARM_Log_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_FARM_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate FARM Time-Series log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_FARM_Time_Series_Log_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_FARM_Sticky_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate FARM Sticky log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_FARM_Sticky_Log_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_FARM_Long_Saved_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate FARM Sticky log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_FARM_Long_Saved_Log_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Mode_Page_Size(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, uint32_t *modePageSize);

    //if using this and not sure if the 6byte or 10 byte command will be used, use this length when allocating your buffer: SCSI_MODE_PAGE_MIN_HEADER_LENGTH + length of mode page from standard
    //This is only needed if not calling the get_SCSI_Mode_Page_Size as that will already take this into account
    #define SCSI_MODE_PAGE_MIN_HEADER_LENGTH (M_Max(MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN, MODE_PARAMETER_HEADER_10_LEN + LONG_LBA_BLOCK_DESCRIPTOR_LEN))

    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Mode_Page(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, const char *logName, const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, bool *used6ByteCmd);

    //This nvme log pull needs lots of proper updates to be more like the SCSI and ATA functions. nvmeLogSizeBytes should be passed as 0 unless you know the length you want to pull.
    // nvmeLogSizeBytes is used since there is not a way to look up the length of most NVMe logs like you can with ATA and SCSI
    OPENSEA_OPERATIONS_API eReturnValues pull_Supported_NVMe_Logs(tDevice *device, uint8_t logNum, eLogPullMode mode, uint32_t nvmeLogSizeBytes);

#if defined(__cplusplus)
}
#endif
