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
// \file logs.h
// \brief This file defines the functions for pulling logs from SCSI and ATA drives

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

    #include <time.h>

    typedef enum _eLogFileNamingConvention
    {
        NAMING_SERIAL_NUMBER_ONLY,
        NAMING_SERIAL_NUMBER_DATE_TIME,//this should be used most of the time to avoid collisions with existing files
        NAMING_OPENSTACK,//not yet implemented
        NAMING_BYUSER,   // a way for the command line user to name the file
    }eLogFileNamingConvention;

    typedef enum _eLogPullMode
    {
        PULL_LOG_RAW_MODE,          // Dump it to stdout. 
        PULL_LOG_BIN_FILE_MODE,     // Create a binary file 
        PULL_LOG_ANALYZE_MODE,      // Humanize the log
    } eLogPullMode;

    OPENSEA_OPERATIONS_API int generate_Logfile_Name(tDevice *device,\
                                                  const char * const logName,\
                                                  const char * const logExtension,\
                                                  eLogFileNamingConvention logFileNamingConvention,\
                                                  char **logFileNameUsed);

    //-----------------------------------------------------------------------------
    //
    //  create_And_Open_Log_File(tDevice *device, FILE *filePtr, char *logName, char *logExtension)
    //
    //! \brief   Description: This function will take the inputs given, generate a file name based off the serial number, time, and other inputs, and open the file for writing.
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle (used to get the serial number)
    //!   \param[in,out] filePtr = File pointer that will hold an open file handle upon successful completion
    //!   \param[in] logPath = this is a directory/folder for where the fle should be created. NULL if current directory. 
    //!   \param[in] logName = this is a name to put into the name of the log. Examples: SMART, CEL
    //!   \param[in] logExtension = this is an ASCII representation of the extension to save the file with. If unsure, use bin. No DOT required in this parameter
    //!   \param[in] logFileNamingConvention = enum value to specify which log file naming convention to use
    //!   \param[out] logFileNameUsed = sets this pointer to the string of the name and extension used for the log file name. This must be set to NULL when calling. Memory should be OPENSEA_PATH_MAX in size
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int create_And_Open_Log_File(tDevice *device,\
                                                    FILE **filePtr,\
                                                    const char * const logPath,\
                                                    const char * const logName,\
                                                    const char * const logExtension,\
                                                    eLogFileNamingConvention logFileNamingConvention,\
                                                    char **logFileNameUsed);

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
    OPENSEA_OPERATIONS_API int get_ATA_Log_Size(tDevice *device, uint8_t logAddress, uint32_t *logFileSize, bool gpl, bool smart);

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
    OPENSEA_OPERATIONS_API int get_SCSI_Log_Size(tDevice *device, uint8_t logPage, uint8_t logSubPage, uint32_t *logFileSize);

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
    OPENSEA_OPERATIONS_API int get_SCSI_VPD_Page_Size(tDevice *device, uint8_t vpdPage, uint32_t *vpdPageSize);

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
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use NULL for current working directory.
    //!   \param[in] featureRegister - this is the feature register for the command. default to zero for most commands.
    //! 
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_ATA_Log(tDevice *device, uint8_t logAddress,\
                                        char *logName, char *fileExtension,\
                                        bool GPL, bool SMART, bool toBuffer,\
                                        uint8_t *myBuf, uint32_t bufSize,\
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
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) NULL if no file output needed
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SCSI_Log(tDevice *device, uint8_t logAddress, uint8_t subpage,\
                                        char *logName, char *fileExtension, bool toBuffer,\
                                        uint8_t *myBuf, uint32_t bufSize,\
                                        const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SCSI_VPD(tDevice *device, uint8_t pageCode, char *logName, char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize)
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
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SCSI_VPD(tDevice *device, uint8_t pageCode, char *logName,\
                                        char *fileExtension, bool toBuffer, uint8_t *myBuf,\
                                        uint32_t bufSize, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_Device_Statistics_Log( tDevice * device )
    //
    //! \brief   Pulls the Device Statistics log
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  filePath - string for the file output path. Set to NULL for the current directory
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Device_Statistics_Log(tDevice * device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_EPC_log( tDevice * device )
    //
    //! \brief   Pulls the Power Conditions log/VPD page
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_EPC_log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_Internal_Status_Log()
    //
    //! \brief   Description:  this function will pull Internal Status logs from an ATA or SCSI device.
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param currentOrSaved - boolean flag to switch between pulling the current log or the saved log (current is currently the only log supported so set this to true)
    //!   \param islDataSet - flag to pull the small, medium, or large dataset. 1 = small, 2 = medium, 3 = large
    //!   \param saveToFile - boolean flag to tell it to save to a file with an auto generated name (naming is based off of serial number and current date and time)
    //!   \param ptrData - pointer to a data buffer. This MUST be non-NULL when saveToFile = false
    //!   \param dataSize - size of the buffer that ptrData points to. This should be at least 256K for the small data set.
    //!   \param [in] filePath = pointer to the path where this log should be generated. Use NULL for current working directory.
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_Internal_Status_Log(tDevice *device,\
                                                bool currentOrSaved,\
                                                uint8_t islDataSet,\
                                                bool saveToFile,\
                                                uint8_t* ptrData,\
                                                uint32_t dataSize,\
                                                const char * const filePath,\
                                                uint32_t transferSizeBytes);

    //-----------------------------------------------------------------------------
    //
    //! get_Pending_Defect_List( tDevice * device )
    //
    //! \brief   Pulls the ACS4/SBC4 Pending Defects log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Pending_Defect_List(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SMART_Extended_Comprehensive_Error_Log( tDevice * device )
    //
    //! \brief   Pulls the SMART Extended Comprehensive Error Log. ATA Only
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SMART_Extended_Comprehensive_Error_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_ATA_DST_Log( tDevice * device )
    //
    //! \brief   Pulls the DST log from an ATA drive. (SMART or GPL log)
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in] extLog - set to true to read the GPL log, false for the SMART log. Recommended you use device->drive_info.ata_Options.generalPurposeLoggingSupported for this value.
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_ATA_DST_Log(tDevice *device, bool extLog, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_DST_Log( tDevice * device )
    //
    //! \brief   Pulls the DST log from an ATA or SCSI device.
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_DST_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_Identify_Device_Data_Log( tDevice * device )
    //
    //! \brief   Pulls the ATA Identify Device Data Log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Identify_Device_Data_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //! get_SATA_Phy_Event_Counters_Log( tDevice * device )
    //
    //! \brief   Pulls the SATA Phy Event Counters Log
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SATA_Phy_Event_Counters_Log(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_SCSI_G_List(tDevice *device)
    //
    //! \brief   Description: This function pulls the G-List from SAS drives using the read defect data 12 command. 
    //                        Pulled with log block address descriptors (LBAs)
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_SCSI_G_List(tDevice *device, const char * const filePath);

    //-----------------------------------------------------------------------------
    //
    //  pull_SCSI_Informational_Exceptions_Log(tDevice *device)
    //
    //! \brief   Description: This function pulls the Informational Exceptions log from a SCSI drive.
    //
    //  Entry:
    //!   \param[in] device = pointer to a valid device structure with a device handle
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_SCSI_Informational_Exceptions_Log(tDevice *device, const char * const filePath);

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
    OPENSEA_OPERATIONS_API int print_Supported_Logs(tDevice *device, uint64_t flags);

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
    OPENSEA_OPERATIONS_API int print_Supported_SCSI_Logs(tDevice *device, uint64_t flags);

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
    OPENSEA_OPERATIONS_API int print_Supported_ATA_Logs(tDevice *device, uint64_t flags);

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
    OPENSEA_OPERATIONS_API int print_Supported_NVMe_Logs(tDevice *device, uint64_t flags);

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
    //!   \param filePath   - path for log file creation (if needed, otherwise set to NULL)
    //  Exit:
    //!   \return SUCCESS = pass, NOT_SUPPORTED = log is not supported by device, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_Generic_Log(tDevice *device, uint32_t logNum, uint32_t subpage,\
        eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes);

    OPENSEA_OPERATIONS_API int pull_Generic_Error_History(tDevice *device, uint8_t bufferID, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes);

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
    OPENSEA_OPERATIONS_API int print_Supported_SCSI_Error_History_Buffer_IDs(tDevice *device, uint64_t flags);

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
    OPENSEA_OPERATIONS_API int get_SCSI_Error_History_Size(tDevice *device, uint8_t bufferID, uint32_t *errorHistorySize, bool createNewSnapshot, bool useReadBuffer16);


    //-----------------------------------------------------------------------------
    //
    //! get_SCSI_Error_History(tDevice *device, uint8_t bufferID, char *logName, bool createNewSnapshot,
    //! char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize,
    //! const char * const filePath);
    //
    //! \brief   generic function to pull an SCSI error history data and save it to a file
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  buffer ID - the buffer ID of the error history data
    //!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) NULL if no file output needed
    //!   \param[in]  createNewSnapshot - set to true to generate a new snapshot when reading the error history directory.
    //!   \param[in]  useReadBuffer16 - use the SPC5 read buffer 16 command
    //!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
    //!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
    //!   \param[in]  myBuf - buffer to return data in if toBuffer is true
    //!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
    //!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SCSI_Error_History(tDevice *device, uint8_t bufferID, char *logName, bool createNewSnapshot, bool useReadBuffer16,\
        char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, \
        const char * const filePath, uint32_t transferSizeBytes, char *fileNameUsed);

    //-----------------------------------------------------------------------------
    //
    //  pull_FARM_Log(tDevice *device, const char * const filePath);
    //
    //! \brief   Description: This function pulls the Seagate FARM log from ATA drives
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use NULL for current working dir.
    //!   \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored. 
    //!                Any other value will specify a transfer size to use to pull SM2. On ATA, this must be a multiple of 512Bytes
    //!   \param[in] issueFactory = if set 0-4 issue the command with the factory feature. 
    //!                             FARM pull Factory subpages   
    //!                             0 – Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
    //!                             1 – Generate and report new FARM data and save to disc(~45ms)(SATA only)
    //!                             2 – Report previous FARM data from disc(~20ms)(SATA only)
    //!                             3 – Report FARM factory data from disc(~20ms)(SATA only)
    //!                             4 - factory subpage (SAS only)
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_FARM_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory);

    //-----------------------------------------------------------------------------
    //
    //  is_FARM_Log_Supported(tDevice *device);
    //
    //! \brief   Description: This function check's if the Seagate FARM log is supported
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_FARM_Log_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API int get_SCSI_Mode_Page_Size(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, uint32_t *modePageSize);

    OPENSEA_OPERATIONS_API int get_SCSI_Mode_Page(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, char *logName, char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, bool *used6ByteCmd);

#if defined(__cplusplus)
}
#endif
