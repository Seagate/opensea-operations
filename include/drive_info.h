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
#pragma once

#include "operations_Common.h"
#include "ata_Security.h"
#include "seagate_operations.h"//for low current spin up info

#if defined (__cplusplus)
extern "C"
{
#endif

    typedef enum _eEncryptionSupport
    {
        ENCRYPTION_NONE,
        ENCRYPTION_FULL_DISK,//FDE Bit
        ENCRYPTION_SELF_ENCRYPTING,//SED drive capable of TCG commands
    }eEncryptionSupport;

    typedef struct _firmwareDownloadSupport
    {
        bool downloadSupported;//also known as immediate
        bool segmentedSupported;
        bool deferredSupported;
        bool dmaModeSupported;
        bool seagateDeferredPowerCycleRequired;//When this is set, a segmented download is treated as a deferred download, requiring a power cycle, in the Seagate drive's firmware.
    }firmwareDownloadSupport;

    typedef struct _humidityInformation //all values are relative humidity percentages from 0% to 100%
    {
        bool humidityDataValid;
        uint8_t currentHumidity;
        bool highestValid;
        uint8_t highestHumidity;//lifetime measured highest
        bool lowestValid;
        uint8_t lowestHumidity;//lifetime measured lowest
    }humidityInformation;

    typedef struct _temperatureInformation //all values in degrees celsius
    {
        bool temperatureDataValid;
        int16_t currentTemperature;
        bool highestValid;
        int16_t highestTemperature;//lifetime measured highest
        bool lowestValid;
        int16_t lowestTemperature;//lifetime measured lowest
    }temperatureInformation;

    typedef struct _lastDSTInformation
    {
        bool informationValid;//If this is not true, then the device doesn't support DST
        uint8_t testNumber;//short/long/background/foreground test number
        uint64_t errorLBA;//error lba if any (if no error, this is set to UINT64_MAX (all F's)
        uint8_t resultOrStatus;//why it failed/still in progress/complete or never run
        uint64_t powerOnHours;//accumulated power on hours at time of DST
    }lastDSTInformation;

    #define MAX_PORTS UINT8_C(2) //Change this number if more ports are added to SAS drives that we want to retrieve the port speed from. For now, 2 is enough - TJE

    typedef enum _eInterfaceSpeedType
    {
        INTERFACE_SPEED_UNKNOWN,
        INTERFACE_SPEED_SERIAL,
        INTERFACE_SPEED_PARALLEL,
        INTERFACE_SPEED_FIBRE, //nothing reported here...
        INTERFACE_SPEED_PCIE,
        INTERFACE_SPEED_ANCIENT,//MFM & RLL...
    }eInterfaceSpeedType;

    typedef struct _ifSerialSpeed
    {
        uint8_t numberOfPorts;//SATA will always be set to 1
        uint8_t activePortNumber;//this will be set to the port number we are currently talking over. This is determined by parsing the accosiation field of a device identification designator. (SPC spec). Note: This is SCSI/SAS only
        uint8_t portSpeedsMax[MAX_PORTS];//0 = not reported, 1 = gen 1 (1.5Gb/s), 2 = gen 2 (3.0Gb/s), 3 = gen 3 (6.0Gb/s), 4 = gen 4 (12.0Gb/s)
        uint8_t portSpeedsNegotiated[MAX_PORTS];//0 = not reported, 1 = gen 1 (1.5Gb/s), 2 = gen 2 (3.0Gb/s), 3 = gen 3 (6.0Gb/s), 4 = gen 4 (12.0Gb/s)
    }ifSerialSpeed;

    typedef enum _eCablingInfoType
    {
        CABLING_INFO_NONE,
        CABLING_INFO_ATA,
    }eCablingInfoType;

    typedef struct _ataCablingInfo
    {
        bool cablingInfoValid;//ATA ID word 93 was valid.
        bool ata80PinCableDetected;//80 pin grounded cabling detected
        bool device1;
        uint8_t deviceNumberDetermined;//0 = reserved, 1 = jumper, 2 = cable select, 3 = other unknown method
    }ataCablingInfo;

#define PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH 20

    typedef struct _ifParallelSpeed
    {
        bool negotiatedValid;//may be false if old parallel interface that doesn't report this...
        double negotiatedSpeed;//MB/s
        double maxSpeed;//MB/s
        bool negModeNameValid;
        char negModeName[PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH];//Hold something like UDMA6, or FAST320, etc
        bool maxModeNameValid;
        char maxModeName[PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH];//Hold something like UDMA6, or FAST320, etc
        eCablingInfoType cableInfoType;
        union {
            ataCablingInfo ataCableInfo;
        };
    }ifParallelSpeed;
    typedef struct _ifFibreSpeed
    {
        uint8_t reserved;//not used since I don't know how to determine this...
    }ifFibreSpeed;
    typedef struct _ifPCIe
    {
        uint8_t reserved;//not used since this isn't determinable right now
    }ifPCIeSpeed;

    typedef struct _ifAncientHistorySpeed
    {
        bool dataTransferGt10MbS;
        bool dataTransferGt5MbSLte10MbS;
        bool dataTransferLte5MbS;
        bool notMFMEncoded;
    }ifAncientHistorySpeed;

    typedef struct _interfaceSpeed
    {
        eInterfaceSpeedType speedType;
        bool speedIsValid;
        union
        {
            ifSerialSpeed serialSpeed;
            ifParallelSpeed parallelSpeed;
            ifFibreSpeed  fiberSpeed;
            ifPCIeSpeed   pcieSpeed;
            ifAncientHistorySpeed ancientHistorySpeed;
        };
    }interfaceSpeed;

    //This struct is only for ATA drives...more specifically legacy drives. Can be used if any ATA drive that populates these fields.
    typedef struct _legacyCHSInfo
    {
        bool legacyCHSValid;
        uint16_t numberOfLogicalCylinders;//Word 1
        uint8_t numberOfLogicalHeads;//Word 3
        uint8_t numberOfLogicalSectorsPerTrack;//Word 6
        bool currentInfoconfigurationValid;//Must be true for the following parts of this structure
        uint16_t numberOfCurrentLogicalCylinders;//Word 54
        uint8_t numberOfCurrentLogicalHeads;//Word 55
        uint8_t numberOfCurrentLogicalSectorsPerTrack;//Word 56
        uint32_t currentCapacityInSectors;//Word 57:58
    }legacyCHSInfo;

    #define MAX_COMPLIANCE_DESCRIPTORS 2
    #define CRYPTO_MODULE_HARDWARE_VERSION_LENGTH 128
    #define CRYPTO_MODULE_VERSION_LENGTH 128
    #define CRYPTO_MODULE_MODULE_NAME_LENGTH 256
    typedef struct _securityCompliance
    {
        bool valid;
        char revision;//2 = FIPS 140-2, 3 = FIPS 140-3
        char overallSecurityLevel;
        char hardwareVersion[CRYPTO_MODULE_HARDWARE_VERSION_LENGTH + 1];
        char version[CRYPTO_MODULE_VERSION_LENGTH + 1];
        char moduleName[CRYPTO_MODULE_MODULE_NAME_LENGTH + 1];
    }securityCompliance;

    //to store common security protocol info from ATA, SCSI, NVMe
    typedef struct _securityProtocolInfo
    {
        bool securityProtocolInfoValid;
        bool tcg;//01-06
        bool cbcs;//07
        bool tapeEncryption;//20
        bool dataEncryptionConfig;//21
        bool saCreationCapabilities;//40
        bool ikev2scsi;//41
        bool sdAssociation;//E7
        bool dmtfSecurity;//E8
        bool nvmeReserved;//E9
        bool nvme;//EA
        bool scsa;//EB
        bool jedecUFS;//EC
        bool sdTrustedFlash;//ED
        bool ieee1667;//EE
        bool ataDeviceServer;//EFh from SAT
        ataSecurityStatus ataSecurityInfo;//to report out info about ATA security for any interface supporting the security protocol.
        securityCompliance cryptoModuleCompliance[MAX_COMPLIANCE_DESCRIPTORS];//setting to 2 for now...not sure there are more than 1 reported currently.
    }securityProtocolInfo;

    #define MAX_FEATURES UINT8_C(60) //change this number if we need to capture more feature support
    #define MAX_FEATURE_LENGTH UINT8_C(50) //maximum number of characters to allow for use when storing feature names.

    #define MAX_SPECS UINT8_C(40)
    #define MAX_SPEC_LENGTH UINT8_C(40)

    typedef struct _driveInformationSAS_SATA
    {
        char modelNumber[MODEL_NUM_LEN + 1];//Null terminated
        char serialNumber[SERIAL_NUM_LEN + 1];//Null terminated
        char pcbaSerialNumber[SERIAL_NUM_LEN + 1];//Unique to Seagate drives at this time.
        char firmwareRevision[FW_REV_LEN + 1];//Null terminated
        char vendorID[T10_VENDOR_ID_LEN + 1];//This is the T10 vendor ID. ATA will be set to "ATA", NVMe will be set to "NVMe"
        char satVendorID[T10_VENDOR_ID_LEN + 1];//Holds the SATL vendor ID
        char satProductID[MODEL_NUM_LEN + 1];//Holds the SATL product ID
        char satProductRevision[FW_REV_LEN + 1];//Holds the SATL product revision
        bool copyrightValid;
        char copyrightInfo[50];//Seagate Specific
        uint64_t worldWideName;
        bool worldWideNameSupported;//set to true when worldWideName contains valid data
        uint64_t worldWideNameExtension;
        bool worldWideNameExtensionValid;//NAA = 6
        temperatureInformation temperatureData;
        humidityInformation humidityData;//SCSI only. Only available when SBC4 or SPC5 are supported
        bool powerOnMinutesValid;//check this to make sure the time is valid. It is possible this is zero on a brand new out of the box product
        uint64_t powerOnMinutes;
        uint64_t maxLBA;
        uint64_t nativeMaxLBA;//ATA Only since SCSI doesn't have a way to get the native max without changing the drive (not ok to do for this function) if set to 0, or UINT64_MAX, then the value is invalid
        legacyCHSInfo ataLegacyCHSInfo;
        bool isFormatCorrupt;//SAS only
        uint32_t logicalSectorSize;//bytes
        uint32_t physicalSectorSize;//bytes
        uint16_t sectorAlignment;//first logical sector offset within the first physical sector
        uint16_t rotationRate;//Value matches the spec. 0 = not reported. 1 = SSD, everything else is an RPM
        uint8_t formFactor;//matches SBC and ACS specs
        uint8_t numberOfSpecificationsSupported;//number of specifications added to the list in the next field
        char specificationsSupported[MAX_SPECS][MAX_SPEC_LENGTH];
        eEncryptionSupport encryptionSupport;
        bool trustedCommandsBeingBlocked;//Linux blocks ATA trusted send/receive commands by default. So this bool is a going to be true on most linux systems that haven't had the kernel boot parameter to allow them set. All other systems will likely see this allowed
        uint64_t cacheSize;//Bytes
        uint64_t hybridNANDSize;//Bytes
        double percentEnduranceUsed; //This is a double so that some drives can report a more finite percentage used with a decimal point.
        uint64_t totalLBAsRead;//LBA count, will need to be multiplied by the logical Sector size in order to know number of bytes
        uint64_t totalLBAsWritten;//LBA count, will need to be multiplied by the logical Sector size in order to know number of bytes
        uint64_t totalWritesToFlash;//SSD Only (SATA only). This is used for calculating write amplification
        uint64_t totalBytesRead;
        uint64_t totalBytesWritten;
        double deviceReportedUtilizationRate;//ACS4 or SBC4 required for this to be valid
        //interface speed (SATA or SAS only)
        interfaceSpeed interfaceSpeedInfo;
        uint8_t numberOfFeaturesSupported;//number of features added in the next field
        char featuresSupported[MAX_FEATURES][MAX_FEATURE_LENGTH];//max of 50 different features, 50 characters allowed for each feature name
        firmwareDownloadSupport fwdlSupport;
        ataSecurityStatus ataSecurityInformation;
        bool readLookAheadSupported;
        bool readLookAheadEnabled;
        bool writeCacheSupported;
        bool writeCacheEnabled;
        bool nvCacheSupported;//SAS only
        bool nvCacheEnabled;//SAS only
        uint8_t smartStatus; //0 = good, 1 = bad, 2 = unknown (unknown will happen on many USB drives, everything else should work)
        uint8_t zonedDevice;//set to 0 for non-zoned devices (SMR). If non-zero, then this matches the latest ATA/SCSI specs for zoned devices
        lastDSTInformation dstInfo;
        bool lowCurrentSpinupValid;//will be set to true for ATA, set to false for SAS
        bool lowCurrentSpinupViaSCT;
        int lowCurrentSpinupEnabled;//only valid when lowCurrentSpinupValid is set to true
        uint64_t longDSTTimeMinutes;//This is the drive's reported Long DST time (if supported). This can be used as an approximate time to read the whole drive on HDD. Not sure this is reliable on SSD since the access isn't limited in the same way a HDD is.
        bool isWriteProtected;//Not available on SATA!
        adapterInfo adapterInformation;//Not populated on all OSs. This is only obtainable from low-level OS calls and copied from the tDevice structure.
        uint8_t lunCount;//for help detecting multi-actuator drives. Will be zero if not checked for, and an invalid value. Will be 1 or more when checked. Each lun represents and actuator today. - TJE
        uint8_t concurrentPositioningRanges;//For multi-actuator drives with multiple actuators per LUN, this will be set to a value greater than zero, if zero then this is not supported.
        bool dateOfManufactureValid;
        uint8_t manufactureWeek;
        uint16_t manufactureYear;
        securityProtocolInfo securityInfo;//TCG, IEEE1667, etc
    }driveInformationSAS_SATA, *ptrDriveInformationSAS_SATA;

    typedef struct _driveInformationNVMe
    {
        //How to get interface speed? (PCIe gen 3, 2, 1, etc)
        //controller data
        struct {
            char modelNumber[MODEL_NUM_LEN + 1];//Null terminated
            char serialNumber[SERIAL_NUM_LEN + 1];//Null terminated
            char firmwareRevision[FW_REV_LEN + 1];//Null terminated
            uint32_t ieeeOUI;//24 bits
            uint16_t pciVendorID;
            uint16_t pciSubsystemVendorID;
            uint16_t controllerID;
            uint16_t majorVersion;//31:16
            uint8_t minorVersion;//15:08
            uint8_t tertiaryVersion;//7:0
            bool hostIdentifierSupported;
            bool hostIdentifierIs128Bits;
            uint8_t hostIdentifier[16];//get features (when supported by drive...and if host has set this)
            uint8_t fguid[16];//128bits...big endian order.
            uint16_t warningCompositeTemperatureThreshold;
            uint16_t criticalCompositeTemperatureThreshold;
            uint8_t totalNVMCapacity[16];//bytes
            double totalNVMCapacityD;//same as above array, but stored in a double
            uint8_t unallocatedNVMCapacity[16];//bytes
            double unallocatedNVMCapacityD;//same as above array, but stored in a double
            uint32_t maxNumberOfNamespaces;
            bool volatileWriteCacheSupported;//from identify
            bool volatileWriteCacheEnabled;//from get features
            uint8_t numberOfFirmwareSlots;
            uint8_t nvmSubsystemNVMeQualifiedName[257];//This is a UTF8 string!
            eEncryptionSupport encryptionSupport;
            uint8_t reserved;//previously part of numberOfControllerFeatures as a word, but changed to a byte
            uint8_t numberOfControllerFeatures;
            char controllerFeaturesSupported[MAX_FEATURES][MAX_FEATURE_LENGTH];//max of 50 different features, 50 characters allowed for each feature name
            uint64_t longDSTTimeMinutes;
            uint8_t numberOfPowerStatesSupported;
        }controllerData;
        //smart log data (controller, not per namespace)
        struct {
            bool valid;
            uint8_t smartStatus; //0 = good, 1 = bad, 2 = unknown (unknown will happen on many USB drives, everything else should work) (this is to be similar to ATA and SCSI-TJE
            bool mediumIsReadOnly;//same as write protect on SCSI
            uint16_t compositeTemperatureKelvin;
            uint8_t percentageUsed;
            uint8_t availableSpacePercent;
            uint8_t availableSpaceThresholdPercent;
            uint8_t dataUnitsRead[16];//in 512B blocks
            double dataUnitsReadD;//Same as above but stored in a double
            uint8_t dataUnitsWritten[16];//in 512B blocks
            double dataUnitsWrittenD;//Same as above but stored in a double
            uint8_t powerOnHours[16];
            double powerOnHoursD;//Same as above but stored in a double
        }smartData;
        //DST information (if supported by the drive...NVMe 1.3)
        lastDSTInformation dstInfo;
        //current namespace data
        struct {
            bool valid;
            uint64_t namespaceSize;//LBAs
            uint64_t namespaceCapacity;//maximum number of logical blocks to be allocated in the namespace at any point in time
            uint64_t namespaceUtilization;//current number of logical blocks allocated in the namespace
            uint32_t formattedLBASizeBytes;
            uint8_t relativeFormatPerformance;//read from the format descritpor
            uint8_t nvmCapacity[16];//bytes
            double nvmCapacityD;//Same as above but stored in a double
            uint8_t namespaceGloballyUniqueIdentifier[16];
            uint64_t ieeeExtendedUniqueIdentifier;
            //Namespace features will include protection information types, and security protocols supported
            uint8_t reserved;//previously part of numberOfControllerFeatures as a word, but changed to a byte
            uint8_t numberOfNamespaceFeatures;
            char namespaceFeaturesSupported[MAX_FEATURES][MAX_FEATURE_LENGTH];//max of 50 different features, 50 characters allowed for each feature name
        }namespaceData;
        securityProtocolInfo securityInfo;//TCG, IEEE1667, etc
    }driveInformationNVMe, *ptrDriveInformationNVMe;

    typedef enum _eDriveInfoType
    {
        DRIVE_INFO_SAS_SATA = 0,
        DRIVE_INFO_NVME
    }eDriveInfoType;

    typedef struct _driveInformation
    {
        eDriveInfoType infoType;
        union
        {
            driveInformationSAS_SATA sasSata;
            driveInformationNVMe nvme;
        };
    }driveInformation, *ptrDriveInformation;

    static M_INLINE void safe_free_drive_info(driveInformation **info)
    {
        safe_Free(M_REINTERPRET_CAST(void**, info));
    }

    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Drive_Information(tDevice *device, ptrDriveInformation driveInfo)
    //
    //! \brief   Description:  This function fills in all the driveInformation into a driveInformation structure
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param driveInfo = pointer to the struct to fill in with ATA drive information.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = one of the operations being called inside of this function failed.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_Drive_Information(tDevice *device, ptrDriveInformationSAS_SATA driveInfo);

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Drive_Information(tDevice *device, ptrDriveInformation driveInfo)
    //
    //! \brief   Description:  This function fills in all the driveInformation into a driveInformation structure
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param driveInfo = pointer to the struct to fill in with SCSI drive information.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = one of the operations being called inside of this function failed.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SCSI_Drive_Information(tDevice *device, ptrDriveInformationSAS_SATA driveInfo);

    OPENSEA_OPERATIONS_API eReturnValues get_NVMe_Drive_Information(tDevice *device, ptrDriveInformationNVMe driveInfo);

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Drive_Information(ptrDriveInformation externalDriveInfo, ptrDriveInformation scsiDriveInfo, ptrDriveInformation ataDriveInfo)
    //
    //! \brief   Description:  This function takes ATA drive information and SCSI drive information and combines it into the externalDriveInfo struct for what we want to show a user about an external drive
    //
    //  Entry:
    //!   \param externalDriveInfo = pointer to the struct to fill in with SCSI and ATA drive information for displaying
    //!   \param scsiDriveInfo = pointer to the struct to filled in with SCSI drive information
    //!   \param ataDriveInfo = pointer to the struct to filled in with SCSI drive information
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void generate_External_Drive_Information(ptrDriveInformationSAS_SATA externalDriveInfo, ptrDriveInformationSAS_SATA scsiDriveInfo, ptrDriveInformationSAS_SATA ataDriveInfo);

    OPENSEA_OPERATIONS_API void generate_External_NVMe_Drive_Information(ptrDriveInformationSAS_SATA externalDriveInfo, ptrDriveInformationSAS_SATA scsiDriveInfo, ptrDriveInformationNVMe nvmeDriveInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_SAS_Sata_Device_Information(ptrDriveInformationSAS_SATA driveInfo)
    //
    //! \brief   Description:  This function is generic and prints out the data in the driveInfo structure to the screen for SAS/SATA drive information type
    //
    //  Entry:
    //!   \param driveInfo = pointer to the driveInformationSAS_Sata struct with information to print to the screen
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_SAS_Sata_Device_Information(ptrDriveInformationSAS_SATA driveInfo);

    OPENSEA_OPERATIONS_API void print_NVMe_Device_Information(ptrDriveInformationNVMe driveInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_Device_Information(ptrDriveInformation driveInfo)
    //
    //! \brief   Description:  This function is generic and prints out the data in the driveInfo structure to the screen.
    //
    //  Entry:
    //!   \param driveInfo = pointer to the driveInformation struct with information to print to the screen
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Device_Information(ptrDriveInformation driveInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_Parent_And_Child_Information(ptrDriveInformation scsiDriveInfo, ptrDriveInformation ataDriveInfo)
    //
    //! \brief   Description:  This function will print out both SCSI reported and ATA reported information. This should only be done for SAT interfaces.
    //
    //  Entry:
    //!   \param scsiDriveInfo = pointer to the driveInformation struct with SCSI information to print to the screen
    //!   \param ataDriveInfo = pointer to the driveInformation struct with ATA information to print to the screen
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Parent_And_Child_Information(ptrDriveInformation scsiDriveInfo, ptrDriveInformation ataDriveInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_Device_Information()
    //
    //! \brief   Description:  This function prints out identifying information about a device.
    //!                        The model number, serial number, current temperature, power on hours,
    //!                        link rate, and maxLBA will be printed to the screen among other things
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param showChildInformation = set to true to show information about both the scsi reported info (what the OS sees) and the ata reported info (what the bridge reads and interprets to show the OS)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = one of the operations being called inside of this function failed.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_Drive_Information(tDevice *device, bool showChildInformation);

    //-----------------------------------------------------------------------------
    //
    //  get_SAS_Interface_Speeds()
    //
    //! \brief   Description:  Function to determine SCSI interface speeds (called by print drive info)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] scsiport0negSpeed = pointer to string for port0 negotiated speed value
    //!   \param[in] scsiport1negSpeed = pointer to string for port1 negotiated speed value
    //!   \param[in] scsiport0maxSpeed = pointer to string for port0 max speed value
    //!   \param[in] scsiport1maxSpeed = pointer to string for port1 max speed value
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    void get_SAS_Interface_Speeds(tDevice *device, char **scsiport0negSpeed, char **scsiport1negSpeed, char **scsiport0maxSpeed, char **scsiport1maxSpeed);

    const char * print_drive_type(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  print_Nvme_Ctrl_Information()
    //
    //! \brief   Description:  Function to print NVMe specific information. 
    //
    //  Entry:
    //!   \param[in] device = device object
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    eReturnValues print_Nvme_Ctrl_Information(tDevice *device);

#if defined (__cplusplus)
}
#endif
