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
// \file format_unit.h
// \brief This file defines the function calls for performing some format unit operations

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported)
    //
    //! \brief   Description:  Checks if format unit is supported and optionally if fast format is supported. (No guarantee on fast format check accuracy at this time)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] fastFormatSupported = bool that will hold if fast format is supported. May be NULL if you don't care about checking for fast format support.
    //!
    //  Exit:
    //!   \return true = format unit supported, false = format unit not supported.
    //
    //-----------------------------------------------------------------------------
    bool is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported);

    //-----------------------------------------------------------------------------
    //
    //  get_Format_Progress(tDevice *device, double *percentComplete)
    //
    //! \brief   Description:  Gets the current progress of a format unit operation
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] percentComplete = pointer to double to hold the current format unit progress.
    //!
    //  Exit:
    //!   \return SUCCESS = format unit not in progress, IN_PROGRESS = format unit in progress, !SUCCESS = something when wrong trying to get progress
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Format_Progress(tDevice *device, double *percentComplete);

    typedef enum _eFormatType
    {
        FORMAT_STD_FORMAT = 0,
        FORMAT_FAST_WRITE_NOT_REQUIRED = 1,
        FORMAT_FAST_WRITE_REQUIRED = 2,//not supported on Seagate Drives at this time.
        FORMAT_RESERVED = 3
    }eFormatType;

    typedef enum _eFormatPattern
    {
        FORMAT_PATTERN_DEFAULT = 0,
        FORMAT_PATTERN_REPEAT = 1,
        //other values are reserved or vendor specific
    }eFormatPattern;

    //-----------------------------------------------------------------------------
    //
    //  int run_Format_Unit(tDevice *device, eFormatType formatType, bool currentBlockSize, uint16_t newBlockSize, uint8_t *gList, uint32_t glistSize, bool completeList, bool disablePrimaryList, bool disableCertification, uint8_t *pattern, uint32_t patternLength, bool securityInitialize, bool pollForProgress)
    //
    //! \brief   Description:  runs or starts a format unit operation on a specified device. All formats through this function are started with immed bit set to 1.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] formatType = use this to specify a standard format or fast format
    //!   \param[in] currentBlockSize = use the current logical block size of the device for format (next parameter ignored)
    //!   \param[in] newBlockSize = if currentBlockSize is false, this is the new logical block size to format the drive with.
    //!   \param[in] gList = pointer to the glist to use during format. When NULL, the device will use the current glist unless the completeList bool is set to true.
    //!   \param[in] glistSize = size of the glist pointed to by gList
    //!   \param[in] completeList = set to true to say the provided glist is the complete glist for the device. If this is true and gList is NULL, then this will clear the glist.
    //!   \param[in] disablePrimaryList = set to true to disable using the primary list during a format.
    //!   \param[in] disableCertification = set to true to disable certification
    //!   \param[in] pattern = pointer to a pattern to use during the format. If NULL, the device's default patter is used.
    //!   \param[in] patternLength = length of the data pointed to by pattern
    //!   \param[in] securityInitialize = set to true to set the security initialize bit which requests previously reallocated areas to be overwritten. (Seagate drive's don't currently support this) SBC spec recommends using Sanitize instead of this bit to overwrite previously reallocated sectors.
    //!   \param[in] pollForProgress = set to true for this function to poll for progress until complete. Set to false to use this function to kick off a format unit operation for you.
    //!
    //  Exit:
    //!   \return SUCCESS = format unit successfull or successfully started, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    typedef struct _runFormatUnitParameters
    {
        eFormatType formatType;
        bool defaultFormat;//default device format. FOV = 0. If combined with disableImmediat, then no data sent to the device. AKA fmtdata bit is zero. Only defect list format, cmplst, and format type will be used.
        bool currentBlockSize;
        uint16_t newBlockSize;
        uint64_t newMaxLBA;//will be ignored if this is set to zero
        uint8_t *gList;
        uint32_t glistSize;
        bool completeList;
        uint8_t defectListFormat;//set to 0 if you don't know or are not sending a list
        bool disablePrimaryList;
        bool disableCertification;
        uint8_t *pattern;
        uint32_t patternLength;
        bool securityInitialize;//Not supported on Seagate products. Recommended to use sanitize instead. This ignores a lot of other fields to perform a secure overwrite of all sectors including reallocated sectors
        bool stopOnListError;//Only used if cmplst is zero and dpry is zero. If the previous condition is met and this is true, the device will stop the format if it cannot access a list, otherwise it will continue processing the command. If unsure, leave false
        bool disableImmediate;//Only set this is you want to wait for the device to completely format itself before returning status! You cannot poll while this is happening. It is recommended that this is left false!
        bool changeProtectionType;//When this is set to true, the protection type below will be used to set parameters going to the drive, otherwise the current settings in the device structure will be used.
        uint8_t protectionType;//if unsure, use 0. This will set the proper bit combinations for each protection type
        uint8_t protectionIntervalExponent;//Only used on protection types 2 or 3. Ignored and unused otherwise since other types require this to be zero. If unsure, leave as zero
    }runFormatUnitParameters;

    OPENSEA_OPERATIONS_API int run_Format_Unit(tDevice *device, runFormatUnitParameters formatParameters, bool pollForProgress);

    //-----------------------------------------------------------------------------
    //
    //  int show_Format_Unit_Progress(tDevice *device)
    //
    //! \brief   Description:  shows the current progress of a format unit operation if one is in progress. - SCSI Format unit
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = format unit was successful, IN_PROGRESS = format unit in progress, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_Format_Unit_Progress(tDevice *device);

    typedef struct _formatStatus
    {
        bool formatParametersAllFs;//This means that the last format failed, or the drive is new, or the data is not available right now
        bool lastFormatParametersValid;
        struct 
        {
            bool isLongList;
            uint8_t protectionFieldUsage;
            bool formatOptionsValid;
            bool disablePrimaryList;
            bool disableCertify;
            bool stopFormat;
            bool initializationPattern;
            bool obsoleteDisableSaveParameters;
            bool immediateResponse;
            bool vendorSpecific;
            uint32_t defectListLength;
            //all options after this are only in the long list
            uint8_t p_i_information;
            uint8_t protectionIntervalExponent;
        }lastFormatData;
        bool grownDefectsDuringCertificationValid;
        uint64_t grownDefectsDuringCertification;//param code 1
        bool totalBlockReassignsDuringFormatValid;
        uint64_t totalBlockReassignsDuringFormat;//param code 2
        bool totalNewBlocksReassignedValid;
        uint64_t totalNewBlocksReassigned;//param code 3
        bool powerOnMinutesSinceFormatValid;
        uint32_t powerOnMinutesSinceFormat;//param code 4
    }formatStatus, *ptrFormatStatus;

    //-----------------------------------------------------------------------------
    //
    //  get_Format_Status(tDevice *device, ptrFormatStatus formatStatus)
    //
    //! \brief   Description:  Gets a SCSI device's format status log information.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] formatStatus = pointer to structure to hold all the information about the last format command that was run and logged by the device.
    //  Exit:
    //!   \return true = changing sector size supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Format_Status(tDevice *device, ptrFormatStatus formatStatus);

    //-----------------------------------------------------------------------------
    //
    //  show_Format_Status_Log(ptrFormatStatus formatStatus);
    //
    //! \brief   Description:  Shows a SCSI device's format status log information.
    //
    //  Entry:
    //!   \param[in] formatStatus = pointer to structure that holds all the information about the last format command that was run and logged by the device.
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_Format_Status_Log(ptrFormatStatus formatStatus);

    //-----------------------------------------------------------------------------
    //
    //  is_Set_Sector_Configuration_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if the device supports changing the sector size. On ATA, this checks if the set configuration ext command is supported. On SCSI, this checks for fast format support.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return true = changing sector size supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Set_Sector_Configuration_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  set_Sector_Configuration(tDevice *device, uint32_t sectorSize)
    //
    //! \brief   Description: Sends the command to quickly change the sector size. On ATA this is the set sector configuration command, on SAS, this is a fast format.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] sectorSize = sector size to change to.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully changed sector size, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_Sector_Configuration(tDevice *device, uint32_t sectorSize);

    typedef struct protectionSupport
    {
        //from inquiry
        bool deviceSupportsProtection;//Must be true for remaining data to be valid. If false, all below this are to be considered false
        //from extended inquiry VPD page
        bool protectionReportedPerSectorSize;//Set to true if a SCSI device supports the supported logical block lengths and protection types VPD page.
        bool protectionType1Supported;
        bool protectionType2Supported;
        bool protectionType3Supported;
        struct _nvmSpecific
        {
            bool nvmSpecificValid;//if true, next fields are valid
            bool piFirst8;
            bool piLast8;
        }nvmSpecificPI;
    }protectionSupport, *ptrProtectionSupport;

    typedef enum _eSectorSizeAddInfoType
    {
        SECTOR_SIZE_ADDITIONAL_INFO_NONE,
        SECTOR_SIZE_ADDITIONAL_INFO_ATA,
        SECTOR_SIZE_ADDITIONAL_INFO_SCSI,
        SECTOR_SIZE_ADDITIONAL_INFO_NVME
    }eSectorSizeAddInfoType;

    typedef struct _sectorSize
    {
        bool valid;
        bool currentFormat;//This is the sector size the drive is currently formatted with
        uint32_t logicalBlockLength;
        eSectorSizeAddInfoType additionalInformationType;
        union
        {
            struct
            {
                uint16_t descriptorCheck;//ATA
                uint8_t descriptorIndex;
            }ataSetSectorFields;
            struct  //SCSI/SAS (Bits that describe how the device supports a logical block length)
            {
                bool piSupportBitsValid;//If true, the fields below are valid, otherwise they were not reported on a sector size basis.
                bool p_i_i_sup;
                bool no_pi_chk;
                bool grd_chk;
                bool app_chk;
                bool ref_chk;
                bool t3ps;
                bool t2ps;
                bool t1ps;
                bool t0ps;
            }scsiSectorBits;
            struct  //these aren't directly related to a specific sector size in NVMe, but are important for formatting information.
            {
                uint8_t relativePerformance;
                uint16_t metadataSize;
            }nvmeSectorBits;
        };
    }sectorSize;

    typedef struct _supportedFormats 
    {
        bool deviceSupportsOtherFormats;//if this is false, then
        bool scsiInformationNotReported;//This flag means, that on a SCSI device, we couldn't get the list of supported sector sizes, so the list includes common known sizes and it most likely a guesstimate. In otherwords, check the product manual
        bool scsiFastFormatSupported;
        protectionSupport protectionInformationSupported;
        struct _nvmMetaDataModes
        {
            bool nvmSpecificValid;
            bool metadataSeparateSup;
            bool metadataXLBASup;
        }nvmeMetadataSupport;
        uint32_t numberOfSectorSizes;//used to know the length of the structure below, set before calling in. On output, this may change if unable to read the same number of sector sizes
        sectorSize sectorSizes[1];//ANYSIZE ARRAY. This means that you should over-allocate this function based on the number of supported sector sizes from the drive.
    }supportedFormats, *ptrSupportedFormats;

    //-----------------------------------------------------------------------------
    //
    //  get_Number_Of_Supported_Sector_Sizes(tDevice *device)
    //
    //! \brief   Description: Gets the number of supported sector sizes on a device. Needed to help allocate memory to read the supported formats.
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] formats = pointer to a list of sectorSize structs to fill and some other protection/formatting information
    //!
    //  Exit:
    //!   \return uint32_t count of the number of supported sector sizes. If 0, then the device doesn't report any way to change sector size or an error occured while trying to determine supported sizes.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API uint32_t get_Number_Of_Supported_Sector_Sizes(tDevice *device);
    
    //-----------------------------------------------------------------------------
    //
    //  get_Supported_Formats(tDevice *device, ptrSupportedFormats formats)
    //
    //! \brief   Description: Gets the devices supported sector sizes and supported protection types that can it can be formatted with.
    //!          //allocate supportedFormats: 
    //!                          formats = malloc(sizeof(ptrSupportedFormats) + numberOfSectorSizes * sizeof(supportedFormats));
    //!                          formats->numberOfSectorSizes = numberOfSectorSizes;
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] formats = pointer to a list of sectorSize structs to fill and some other protection/formatting information
    //!
    //  Exit:
    //!   \return SUCCESS = successfully got the reported sector sizes, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Supported_Formats(tDevice *device, ptrSupportedFormats formats);

    //-----------------------------------------------------------------------------
    //
    //  show_Supported_Formats(ptrSupportedFormats formats)
    //
    //! \brief   Description: Shows the sector sizes and protection types available on a device.
    //
    //  Entry:
    //!   \param[in] formats = pointer to the formats that were read already.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_Supported_Formats(ptrSupportedFormats formats);

    //-----------------------------------------------------------------------------
    //
    //  ata_Map_Sector_Size_To_Descriptor_Check(tDevice *device, uint32_t logicalBlockLength, uint16_t *descriptorCheckCode, uint8_t *descriptorIndex)
    //
    //! \brief   Description: Takes a sector size that is requested and maps it to a descriptor in the sector size log on ATA to set the correct parameters to use in the set sector configuration command.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logicalBlockLength = the logical block length you want to change to
    //!   \param[out] descriptorCheckCode = pointer to a word that will hold the descriptor check code to send to the device
    //!   \param[out] descriptorIndex = pointer to a byte that will hold the descriptor index to send to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully mapped sector size to a descriptor from the device, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Map_Sector_Size_To_Descriptor_Check(tDevice *device, uint32_t logicalBlockLength, uint16_t *descriptorCheckCode, uint8_t *descriptorIndex);

#if !defined (DISABLE_NVME_PASSTHROUGH)

    typedef enum _nvmFmtSecureErase
    {
        NVM_FMT_SE_NO_SECURE_ERASE_REQUESTED,
        NVM_FMT_SE_USER_DATA, //may perform cryptographic erase if the controller supports it.
        NVM_FMT_SE_CRYPTO //requires the controller to support cryptographic erase.
    }nvmFmtSecureErase;

    typedef struct _nvmFmtPILocation
    {
        bool valid;//if this is not set, then it is assumed to use the last 8 bytes of metadata (PIL bit cleared to zero) or whatever the device supports
        bool first8Bytes;//set to true for PI data in first 8 bytes of metadata, false for last 8 bytes.
    }nvmFmtPILocation;

    typedef struct _nvmFmtMetadataSettings
    {
        bool valid;//if this is not set, then it is assumed to use a separate metadata buffer. (or whatever the device supports)
        bool metadataAsExtendedLBA;//Set to true for metadata to be transferred as part of an extended data LBA. False to transfer it as a separate buffer.
    }nvmFmtMetadataSettings;

    typedef struct _nvmFmtSize
    {
        bool currentBlockSize;//will reuse the same LBA format that it is already formatted as.
        uint32_t newBlockSize;
        bool changeMetadataSize;//if true, then the next parameter will be analyzed, otherwise a compatible sector size with the specified metadata size will be chosen
        uint16_t metadataSize;
    }nvmFmtSize;

    typedef struct _runNVMFormatParameters
    {
        bool currentNamespace;//This will only work if the controller supports formatting only the current namespace. Check FNA bits
        bool formatNumberProvided;//set tot true when specifying one of the 16 formats a NVMe device reports in it's identify namespace data. If false, then the sector size will be mapped as best it can with other inputs
        union {
            nvmFmtSize newSize;//NOTE: NVMe can report the same sector size with differing metadata sizes so need to map incoming size to 
            uint8_t formatNumber;//0-15
        };
        nvmFmtSecureErase secureEraseSettings;
        nvmFmtPILocation protectionLocation;
        bool changeProtectionType;//set to true if switching PI type from current format
        uint8_t protectionType;//if unsure, use 0. This will set the proper bit combinations for each protection type
        nvmFmtMetadataSettings metadataSettings;
    }runNVMFormatParameters;

    //-----------------------------------------------------------------------------
    //
    //  run_NVMe_Format(tDevice * device, runNVMFormatParameters nvmParams, bool pollForProgress)
    //
    //! \brief   Description:  Function to help send NVMe Format command. 
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] nvmParams = parameters to control how to send the format and what format to switch to
    //!   \param[in] pollForProgress = set to true for this function to poll for progress until complete, false to exit after sending the format command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_NVMe_Format(tDevice * device, runNVMFormatParameters nvmParams, bool pollForProgress);

    //-----------------------------------------------------------------------------
    //
    //  get_NVM_Format_Progress(tDevice *device, double *percentComplete)
    //
    //! \brief   Description: Gets the percent complete of a NVM Format operation 
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] percentComplete = value that holds the percentage that a format is complete.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_NVM_Format_Progress(tDevice *device, uint8_t *percentComplete);

    //-----------------------------------------------------------------------------
    //
    //  show_NVM_Format_Progress(tDevice *device)
    //
    //! \brief   Description:  Gets and shows the progress of an NVM format to the screen
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_NVM_Format_Progress(tDevice *device);

#endif

#if defined (__cplusplus)
}
#endif
