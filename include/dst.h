// SPDX-License-Identifier: MPL-2.0

//! \file dst.h
//! \brief Defines functions, enums, types, etc. for performing Device Self Tests (DST) on devices.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "operations_Common.h"
#include "sector_repair.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //! \enum eDSTTypeEnum
    //! \brief Enum specifying the type of Device Self Test (DST) to perform.
    //!
    //! This enum specifies the type of DST to perform. The short and long tests are defined by ATA and SCSI standards.
    //! \note offline may also be called background in some specs
    //! \note captive may also be called foreground in some specs
    typedef enum eDSTTypeEnum
    {
        /*!< Short DST. 2 Minutes or less to complete */
        DST_TYPE_SHORT = 1,
        /*!< Long DST. All Short DST actions + read all disc LBAs */
        DST_TYPE_LONG = 2,
        /*!< Obsolete due to misspelling. Use corrected spelling \a DST_TYPE_CONVEYANCE */
        DST_TYPE_CONVEYENCE = 3,
        /*!< Conveyance DST. Used to detect handling damage on ATA products that support this. */
        DST_TYPE_CONVEYANCE = 3,
    } eDSTType;

    //! \fn eReturnValues run_DST(tDevice* device, eDSTType DSTType, bool pollForProgress, bool captiveForeground,
    //! bool ignoreMaxTime)
    //! \brief Runs a Device Self Test (DST) on the specified device.
    //! \details This function can send short, long, or conveyance DST and poll for progress on background/offline
    //! tests. If the test is captive/foreground, it will wait for the test to complete before returning.
    //! \param[in] device Pointer to the device structure representing the device to run the DST on.
    //! \param[in] DSTType The type of DST to run (short, long, conveyance).
    //! \param[in] pollForProgress Set to true to poll for progress and display it on the screen.
    //! \param[in] captiveForeground Set to true to run the DST in captive/foreground mode, waiting for completion.
    //! \param[in] ignoreMaxTime Set to true to ignore the maximum time limit for the DST. By default if a
    //! DST is taking too long to complete in background/offline mode, it will be aborted after a certain time as the
    //! drive may be hung and unable to complete the DST.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues
    run_DST(tDevice* device, eDSTType DSTType, bool pollForProgress, bool captiveForeground, bool ignoreMaxTime);

    //! \fn eReturnValues send_DST(tDevice* device, eDSTType DSTType, bool captiveForeground, uint32_t commandTimeout)
    //! \brief Sends a Device Self Test (DST) command to the specified device.
    //! \param[in] device Pointer to the device structure representing the device to send the DST command to.
    //! \param[in] DSTType The type of DST to send (short, long, conveyance).
    //! \param[in] captiveForeground Set to true to run the DST in captive/foreground mode, waiting for completion.
    //! \param[in] commandTimeout The timeout for the command in milliseconds.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues send_DST(tDevice* device,
                                                  eDSTType DSTType,
                                                  bool     captiveForeground,
                                                  uint32_t commandTimeout);

    //! \fn eReturnValues abort_DST(tDevice* device)
    //! \brief Sends a Device Self Test (DST) abort command to the specified device.
    //! \param[in] device Pointer to the device structure representing the device to send the DST abort command to.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues abort_DST(tDevice* device);

    //! \fn eReturnValues get_DST_Progress(tDevice* device, uint32_t* percentComplete, uint8_t* status)
    //! \brief Gets the progress of an active DST test
    //!
    //! \param[in] device Pointer to the device structure representing the device to get the DST progress from.
    //! \param[out] percentComplete Pointer to a uint32_t that will hold the DST percentage complete.
    //! 0% = just started, 100% = completed.
    //! \param[out] status Pointer to a uint8_t that will hold the current DST status. 0h - Fh are valid status codes.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_DST_Progress(tDevice* device, uint32_t* percentComplete, uint8_t* status);

    //! \fn eReturnValues print_DST_Progress(tDevice* device)
    //! \brief Prints the progress of an active Device Self Test (DST) to the screen in human-readable format.
    //! This will translated the percentage and status according to the device's standard for easy interpretation.
    //! \param[in] device Pointer to the device structure representing the device to print the DST progress for.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues print_DST_Progress(tDevice* device);

//! \def MAX_DST_STATUS_STRING_LENGTH
//! \brief The maximum length of the string used to translate the DST status.
//! This is used to ensure that the translated status string does not exceed the buffer size.
#define MAX_DST_STATUS_STRING_LENGTH (160)

    //! \fn void translate_DST_Status_To_String(uint8_t status, char* translatedString, bool justRanDST,
    //! bool isNVMeDrive)
    //! \brief Translates the Device Self Test (DST) status code into a human-readable string.
    //! \param[in] status The DST status code to translate.
    //! \param[out] translatedString Pointer to a buffer where the translated string will be stored. Must be
    //! MAX_DST_STATUS_STRING_LENGTH in size.
    //! \param[in] justRanDST Set to true if the DST was just run, false otherwise. This changes the output slightly
    //! for clarity of status code 0h.
    //! \param[in] isNVMeDrive Set to true if the device is an NVMe drive, false otherwise. This looks up the NVMe
    //! status code since they vary slightly from SATA and SAS
    //! \return void
    M_NONNULL_PARAM_LIST(2)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API
    void translate_DST_Status_To_String(uint8_t status, char* translatedString, bool justRanDST, bool isNVMeDrive);

    //! \fn eReturnValues get_Long_DST_Time(tDevice* device, uint8_t* hours, uint8_t* minutes)
    //! \brief Gets the long DST timeout in hours and minutes
    //!
    //! \param[in] device Pointer to the device structure representing the device to get the long DST time from.
    //! \param[out] hours Pointer to a uint8_t that will hold the number of hours for the long DST timeout.
    //! \param[out] minutes Pointer to a uint8_t that will hold the number of minutes for the long DST timeout.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Long_DST_Time(tDevice* device, uint8_t* hours, uint8_t* minutes);

    //! \fn eReturnValues ata_Abort_DST(tDevice* device)
    //! \brief Sends an ATA Device Self Test (DST) abort command to the specified device.
    //! \param[in] device Pointer to the device structure representing the device to send the DST abort command to.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues ata_Abort_DST(tDevice* device);

    //! \fn eReturnValues ata_Get_DST_Progress(tDevice* device, uint32_t* percentComplete, uint8_t* status)
    //! \brief Gets the ATA DST progress as percent complete rather than percent
    //! remaining as the ATA standard does. This makes the report match both SCSI and NVMe progress reports.
    //! \param[in] device Pointer to the device structure representing the device to get the DST progress from.
    //! \param[out] percentComplete Pointer to a uint32_t that will hold the percentage complete of the DST.
    //! \param[out] status Pointer to a uint8_t that will hold the current DST status/result.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues ata_Get_DST_Progress(tDevice*  device,
                                                              uint32_t* percentComplete,
                                                              uint8_t*  status);

    //! \fn eReturnValues scsi_Get_DST_Progress(tDevice* device, uint32_t* percentComplete, uint8_t* status)
    //! \brief Gets the SCSI DST progress as percent complete.
    //! \param[in] device Pointer to the device structure representing the device to get the DST progress from.
    //! \param[out] percentComplete Pointer to a uint32_t that will hold the percentage complete of the DST.
    //! \param[out] status Pointer to a uint8_t that will hold the current DST status/result.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues scsi_Get_DST_Progress(tDevice*  device,
                                                               uint32_t* percentComplete,
                                                               uint8_t*  status);

    //! \fn eReturnValues nvme_Get_DST_Progress(tDevice* device, uint32_t* percentComplete, uint8_t* status)
    //! \brief Gets the NVMe DST progress as percent complete.
    //! \param[in] device Pointer to the device structure representing the device to get the DST progress from.
    //! \param[out] percentComplete Pointer to a uint32_t that will hold the percentage complete of the DST.
    //! \param[out] status Pointer to a uint8_t that will hold the current DST status/result.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API eReturnValues nvme_Get_DST_Progress(tDevice*  device,
                                                               uint32_t* percentComplete,
                                                               uint8_t*  status);

    //! \fn eReturnValues scsi_Abort_DST(tDevice* device)
    //! \brief Sends a SCSI Device Self Test (DST) abort command to the specified device.
    //! \param[in] device Pointer to the device structure representing the device to send the DST abort command to.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues scsi_Abort_DST(tDevice* device);

    //! \fn eReturnValues nvme_Abort_DST(tDevice* device, uint32_t nsid)
    //! \brief Sends a NVMe Device Self Test (DST) abort command to the specified device.
    //! \param[in] device Pointer to the device structure representing the device to send the DST abort command to.
    //! \param[in] nsid The namespace ID to use for the NVMe abort command. Can be a specific namespace or
    //! all namespaces value depending on how the DST was started.
    //! \return eReturnValues indicating the success or failure of the operation.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues nvme_Abort_DST(tDevice* device, uint32_t nsid);

    //! \fn bool get_Error_LBA_From_DST_Log(tDevice* device, uint64_t* lba)
    //! \brief Gets the error LBA from the Device Self Test (DST) log of the specified device.
    //! \param[in] device Pointer to the device structure representing the device to get the error LBA from.
    //! \param[out] lba Pointer to a uint64_t that will hold the LBA of the last error.
    //! \return true if a valid LBA was returned, false if the LBA is invalid or could not be read found in the log.
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1) M_PARAM_WO(2) OPENSEA_OPERATIONS_API bool get_Error_LBA_From_DST_Log(tDevice* device, uint64_t* lba);

    //! \struct dstAndCleanErrorList
    //! \brief Structure to hold the error list and current index for DST and clean operations.
    //! This structure is used to pass the error list and index to the run_DST_And_Clean function.
    typedef struct s_dstAndCleanErrorList
    {
        //! \var ptrErrorLBA Pointer to the list of error LBAs found during DST and clean operations.
        //! \brief This is used to store the LBAs that had errors during the DST and clean process.
        ptrErrorLBA ptrToErrorList;
        //! \var errorIndex Current offset/index into the error list.
        //! \brief This is used to track the current position in the error list for processing.
        uint64_t* errorIndex;
    } dstAndCleanErrorList, *ptrDSTAndCleanErrorList;

    //! \fn eReturnValues run_DST_And_Clean(tDevice* device, uint16_t errorLimit, custom_Update updateFunction,
    //! void* updateData, ptrDSTAndCleanErrorList externalErrorList, bool* repaired)
    //! \brief Runs a Device Self Test (DST) and cleans the device by repairing errors found during the DST.
    //! \details This function performs a DST, retrieves the error LBA, and attempts to repair it. After each repair,
    //! it reads +-5000 LBAs to check for additional errors to repair. It then restarts the DST until all errors
    //! are repaired or the error limit is reached. If an error cannot be repaired, this function returns a failure
    //! code to indicate that it cannot be repaired. Repairs can fail due to OS permissions blocking access to the
    //! LBA, or the device running out of spare sectors to repair the LBA. If DST returns an error for a mechanical
    //! or electrical issue, this function will return FAILURE to indicate that the device is not repairable.
    //! \param[in] device Pointer to the device structure representing the device to run the DST and clean on.
    //! \param[in] errorLimit The maximum number of errors to repair. Must be 1 or higher.
    //! \param[in] updateFunction Optional custom update function to call during the process. (Unused at this time)
    //! \param[in] updateData Optional data to pass to the custom update function. (Unused at this time)
    //! \param[in] externalErrorList Optional pointer to an external error list to use for storing errors found during
    //! the DST and clean process. If this is provided, the function will not print the ending result error list.
    //! \param[out] repaired Pointer to a boolean that will be set to true if the device was repaired, false otherwise
    //! \return eReturnValues indicating the success or failure of the operation. Returns SUCCESS if all errors were
    //! repaired, otherwise returns a failure code indicating the error limit was reached or an unrepairable condition
    //! was encountered.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
    OPENSEA_OPERATIONS_API eReturnValues run_DST_And_Clean(tDevice*                device,
                                                           uint16_t                errorLimit,
                                                           custom_Update           updateFunction,
                                                           void*                   updateData,
                                                           ptrDSTAndCleanErrorList externalErrorList,
                                                           bool*                   repaired);

    //! \struct dstDescriptor
    //! \brief Structure to hold the Device Self Test (DST) descriptor.
    //! This structure is used to store the results of a DST, including self-test status, lifetime timestamp,
    //! power-on hours, and other relevant information that the device can report in a given DST log entry.
    typedef struct s_dstDescriptor
    {
        //! \var descriptorValid
        //! \brief Indicates whether the DST descriptor is valid. If false, the rest of the fields are not valid.
        //! This is used to check if the descriptor was successfully read from the device.
        bool descriptorValid;
        //! \var selfTestType
        //! \brief The type of self-test that was performed. This can be short, long, conveyance, etc.
        eDSTType selfTestType;
        //! \var selfTestRun
        //! \brief "Content of LBA 0:7" in ATA spec, or from self test status in NVMe
        uint8_t selfTestRun;
        //! \var selfTestExecutionStatus
        //! \brief The execution status of the self-test. This can indicate whether the test passed, failed, or was
        //! aborted, etc.
        uint8_t selfTestExecutionStatus;
        union
        {
            //! \var lifetimeTimestamp
            //! \brief The lifetime timestamp of the device when the self-test was performed.
            uint64_t lifetimeTimestamp;
            //! \var powerOnHours
            //! \brief The number of power-on hours of the device when the self-test was performed.
            //! This is used to indicate how long the device has been powered on since its last reset or power cycle.
            uint64_t powerOnHours;
        };
        union
        {
            //! \var checkPointByte
            //! \brief The checkpoint byte for the self-test. This is used to indicate the step of the self-test which
            //! encountered an error or was last completed. This is a vendor specific value and has no standard meaning.
            uint8_t checkPointByte;
            //! \var segmentNumber
            //! \brief The segment number for the self-test. This is used to indicate which segment of the self-test
            //! encountered an error or was last completed. This is a vendor specific value and has no standard meaning.
            uint8_t segmentNumber;
        };
        union
        {
//! \def ATA_VENDOR_SPECIFIC_DATA_SIZE
//! \brief The size of the ATA vendor specific data in a DST log entry
#define ATA_VENDOR_SPECIFIC_DATA_SIZE (15)
            //! \var ataVendorSpecificData
            //! \brief The vendor specific data for ATA devices. This is used to store additional information that
            //! the device manufacturer may provide in the DST log entry.
            //! \note This is 15 bytes in size, as per ATA specification.
            uint8_t ataVendorSpecificData[ATA_VENDOR_SPECIFIC_DATA_SIZE];
            //! \var scsiVendorSpecificByte
            //! \brief The vendor specific byte for SCSI devices. This is used to store additional information that
            //! the device manufacturer may provide in the DST log entry.
            //! \note This is a single byte in size, as per SCSI specification.
            uint8_t scsiVendorSpecificByte;
            //! \var nvmeVendorSpecificWord
            //! \brief The vendor specific word for NVMe devices. This is used to store additional information that
            //! the device manufacturer may provide in the DST log entry.
            //! \note This is a single 16-bit word in size, as per NVMe specification.
            uint16_t nvmeVendorSpecificWord;
        };
        //! \var lbaOfFailure
        //! \brief The LBA (Logical Block Address) of the failure encountered during the self-test.
        //! \note This is used to indicate the specific LBA that caused the self-test to fail or encounter an error.
        //! If this is set to all F's, it indicates that no specific LBA was identified as the cause of the failure.
        uint64_t lbaOfFailure;
        //! \var nsidValid
        //! \brief Indicates whether the namespace ID is valid. This is used for NVMe devices to indicate if the
        //! namespace ID is valid for the self-test.
        //! \note If this is false, the namespace ID field should not be used.
        bool nsidValid;
        //! \var namespaceID
        //! \brief The namespace ID for NVMe devices. This is used to indicate which namespace the self-test was
        //! performed on.
        //! \note This is only valid if nsidValid is true.
        uint32_t namespaceID;
        union
        {
            //! \var scsiSenseCode
            //! \brief The SCSI sense code for the self-test. This is used to indicate the specific error or status
            //! encountered during the self-test.
            //! \note This is used for SCSI devices to provide detailed information about the self-test result.
            //! \note ATA devices will fill this in according to the SAT specification.
            struct
            {
                //! \var senseKey
                //! \brief The SCSI sense key for the self-test. See SPC for details on sense keys.
                uint8_t senseKey;
                //! \var additionalSenseCode
                //! \brief The additional sense code for the self-test. See SPC for details.
                uint8_t additionalSenseCode;
                //! \var additionalSenseCodeQualifier
                //! \brief The additional sense code qualifier for the self-test. See SPC for details.
                uint8_t additionalSenseCodeQualifier;
            } scsiSenseCode;
            //! \var nvmeStatus
            //! \brief The NVMe status for the self-test. This is used to indicate the specific error or status
            //! encountered during the self-test.
            //! \note This is used for NVMe devices to provide detailed information about the self-test result.
            struct
            {
                //! \var statusCodeValid
                //! \brief Indicates whether the NVMe status code is valid. If false, the status code is not valid.
                //! This is used to check if the NVMe status was successfully read from the device.
                bool statusCodeValid;
                //! \var statusCodeTypeValid
                //! \brief Indicates whether the NVMe status code type is valid. If false, the status code type is not
                //! valid. This is used to check if the NVMe status code type was successfully read from the device.
                bool statusCodeTypeValid;
                //! \var statusCode
                //! \brief The NVMe status code for the self-test. See NVMe specification for details on status codes.
                uint8_t statusCode;
                //! \var statusCodeType
                //! \brief The NVMe status code type for the self-test. See NVMe specification for details on status
                //! code types.
                uint8_t statusCodeType;
            } nvmeStatus;
        };
    } dstDescriptor, *ptrDescriptor;

//! \def MAX_DST_ENTRIES
//! \brief The maximum number of Device Self Test (DST) entries read from the DST log.
//! This is used to define the size of the dstLogEntries structure.
//! \note The ATA specification allows for a maximum of 21 entries in the SMART log and 19 per GPL page.
//! I have only ever seen 21 supported via GPL access, but the spec allows for 2048 in SATA
//! \note The NVMe specification allows for a maximum of 20 entries in the DST log.
#define MAX_DST_ENTRIES (21)

    //! \enum dstLogType
    //! \brief Enum specifying the type of Device Self Test (DST) log entries.
    //! This is used to differentiate between different types of DST logs, such as ATA, SCSI, and NVMe.
    //! \note This is used to help parse the log entries correctly based on the type of device.
    typedef enum dstLogTypeEnum
    {
        /*!< Unknown DST log type or empty entry */
        DST_LOG_TYPE_UNKNOWN = 0,
        /*!< Entry from an ATA device */
        DST_LOG_TYPE_ATA,
        /*!< Entry from a SCSI device */
        DST_LOG_TYPE_SCSI,
        /*!< Entry from an NVMe device */
        DST_LOG_TYPE_NVME,
    } dstLogType;

    //! \struct dstLogEntries
    //! \brief Structure to hold the Device Self Test (DST) log entries, up to MAX_DST_ENTRIES.
    //! \details This structure is used to store the results of multiple DST log entries, including their type and
    //! the actual entries. It allows for easy retrieval and processing of DST log entries from the device.
    //! \note The numberOfEntries field indicates how many valid entries are present in the dstEntry array.
    //! \note This is used to retrieve and store the DST log entries from the device.
    typedef struct s_dstLogEntries
    {
        //! \var numberOfEntries
        //! \brief The number of valid entries in the dstEntry array.
        //! This is used to indicate how many DST log entries were successfully read from the device.
        uint8_t numberOfEntries;
        //! \var logType
        //! \brief The type of DST log entries contained in this structure.
        //! This is used to indicate whether the entries are from an ATA, SCSI, or NVMe device.
        //! This can be used by software interpreting this data to parse things correctly.
        dstLogType logType;
        //! \var dstEntry
        //! \brief The array of DST log entries, up to MAX_DST_ENTRIES.
        //! This is used to store the actual DST log entries read from the device.
        //! Each entry contains information about the self-test status, lifetime timestamp, power-on hours, etc.
        dstDescriptor dstEntry[MAX_DST_ENTRIES];
    } dstLogEntries, *ptrDstLogEntries;

    //! \fn eReturnValues get_DST_Log_Entries(tDevice* device, ptrDstLogEntries entries)
    //! \brief Retrieves the Device Self Test (DST) log entries from the specified device.
    //! \details This function reads the DST log entries from the device and stores them in the provided entries
    //! structure. It can handle ATA, SCSI, and NVMe devices and will populate the logType field accordingly.
    //! \param[in] device Pointer to the device structure representing the device to get the DST log entries from.
    //! \param[out] entries Pointer to a dstLogEntries structure where the retrieved DST log entries will be stored.
    //! \return eReturnValues indicating the success or failure of the operation. Returns SUCCESS if the log entries
    //! were successfully retrieved, otherwise returns a failure code indicating the error encountered.
    //!
    //! \note The entries structure must be allocated and passed to this function. It will be filled with the DST log
    //! entries read from the device.
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues get_DST_Log_Entries(tDevice* device, ptrDstLogEntries entries);

    //! \fn eReturnValues print_DST_Log_Entries(ptrDstLogEntries entries)
    //! \brief Prints the Device Self Test (DST) log entries to the screen in a human-readable format.
    //! \details This function takes the DST log entries retrieved from the device and prints them to the screen,
    //! translating the status codes and other information into a format that is easy to understand.
    //! \param[in] entries Pointer to a dstLogEntries structure containing the DST log entries to print.
    //! \return eReturnValues indicating the success or failure of the operation. Returns SUCCESS if the log entries
    //! were successfully printed, otherwise returns a failure code indicating the error encountered.
    //!
    //! \note The entries structure must be valid and contain the DST log entries to be printed.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues print_DST_Log_Entries(ptrDstLogEntries entries);

    //! \fn bool is_Self_Test_Supported(tDevice* device)
    //! \brief Checks if the Device Self Test (DST) is supported on the specified device.
    //! \details This function checks if the device supports any form of self-test functionality.
    //! \param[in] device Pointer to the device structure representing the device to check for DST support.
    //! \return true if the device supports self-test functionality, false otherwise.
    //!
    //! \note This function checks for both ATA and SCSI devices to determine if self-test is supported.
    //! It does not check for specific types of self-tests (short, long, conveyance), just that self-test is supported.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_Self_Test_Supported(tDevice* device);

    //! \fn bool is_Conveyence_Self_Test_Supported(tDevice* device)
    //! \brief Deprecated. Use \a is_Conveyance_Self_Test_Supported() instead.
    //! \note Deprecated since function has incorrect spelling in the name.
    //! This will call the correct spelling function for you, but will eventually be removed.
    M_DEPRECATED M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API
        bool is_Conveyence_Self_Test_Supported(tDevice* device);

    //! \fn bool is_Conveyance_Self_Test_Supported(tDevice* device)
    //! \brief Checks if the Conveyance Self Test is supported on the specified device.
    //! \details This function checks if the device supports the conveyance self-test functionality.
    //! \param[in] device Pointer to the device structure representing the device to check for conveyance self-test
    //! support.
    //! \return true if the device supports conveyance self-test functionality, false otherwise.
    //! \note Only ATA devices have a conveyance self-test, SCSI and NVMe do not support this.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_Conveyance_Self_Test_Supported(tDevice* device);

    //! \fn bool is_Selective_Self_Test_Supported(tDevice* device)
    //! \brief Checks if the Selective Self Test is supported on the specified device.
    //! \details This function checks if the device supports the selective self-test functionality.
    //! \param[in] device Pointer to the device structure representing the device to check for selective self-test
    //! support.
    //! \return true if the device supports selective self-test functionality, false otherwise.
    //! \note Only ATA devices have a selective self-test, SCSI and NVMe do not support this.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_Selective_Self_Test_Supported(tDevice* device);

    //! \fn eReturnValues run_SMART_Offline(tDevice* device)
    //! \brief Runs a SMART Offline Data Collection on the specified device.
    //! \details This function initiates a SMART Offline Self Test on the device, which is a background test that
    //! checks the health of the device without interrupting normal operations. It is typically used to update
    //! the SMART attributes and perform a quick check of the device's health.
    //! \param[in] device Pointer to the device structure representing the device to run the SMART Offline test on.
    //! \return eReturnValues indicating the success or failure of the operation. Returns SUCCESS if the test was
    //! successfully initiated, otherwise returns a failure code indicating the error encountered.
    //!
    //! \note Only ATA devices support SMART Offline Data Collection. SCSI and NVMe devices do not have this
    //! \note Modern ATA devices do not need this as they regularly update SMART attributes in the background.
    //! \note Some older ATA devices may not need this if they support the SMART auto-offline feature and it is enabled
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues run_SMART_Offline(tDevice* device);

#if defined(__cplusplus)
}
#endif
