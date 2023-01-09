//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 

#include "nvme_operations.h"

void nvme_Print_Feature_Identifiers_Help(void)
{
    printf("\n====================================================\n");
    printf(" Feature\t O/M \tPersistent\tDescription\n");
    printf("Identifier\t   \tAcross Power\t      \n");
    printf("          \t   \t  & Reset   \t      \n");
    printf("====================================================\n");
    printf("00h       \t   \t            \tReserved\n");
    printf("01h       \t M \t   NO       \tArbitration\n");
    printf("02h       \t M \t   NO       \tPower Management\n");
    printf("03h       \t O \t   YES      \tLBA Range Type\n");
    printf("04h       \t M \t   NO       \tTemprature Threshold\n");
    printf("05h       \t M \t   NO       \tError Recovery\n");
    printf("06h       \t O \t   NO       \tVolatile Write Cache\n");
    printf("07h       \t M \t   NO       \tNumber of Queues\n");
    printf("08h       \t M \t   NO       \tInterrupt Coalescing\n");
    printf("09h       \t M \t   NO       \tInterrupt Vector Configuration\n");
    printf("0Ah       \t M \t   NO       \tWrite Atomicity Normal\n");
    printf("0Bh       \t M \t   NO       \tAsynchronous Event Configuration\n");
    printf("0Ch       \t O \t   NO       \tAutonomous Power State Transition\n");
    printf("0Dh       \t O \t   NO       \tHost Memory Buffer\n");
    printf("0Eh-77h   \t   \t            \tReserved          \n");
    printf("78h-7Fh   \t   \t            \tRefer to NVMe Management Spec\n");
    printf("80h-BFh   \t   \t            \tCommand Set Specific (Reserved)\n");
    printf("C0h-FFh   \t   \t            \tVendor Specific\n");
    printf("====================================================\n");
}

int nvme_Print_All_Feature_Identifiers(tDevice *device, eNvmeFeaturesSelectValue selectType, M_ATTR_UNUSED bool listOnlySupportedFeatures)
{
    int ret = UNKNOWN;
    uint8_t featureID;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    printf(" Feature ID\tRaw Value\n");
    printf("===============================\n");
    for (featureID = 1; featureID < 0x0C; featureID++)
    {
        if (featureID == NVME_FEAT_LBA_RANGE_)
        {
            continue;
        }
        memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
        featureCmd.fid = featureID;
        featureCmd.sel = selectType;
        if (nvme_Get_Features(device, &featureCmd) == SUCCESS)
        {
            printf("    %02Xh    \t0x%08X\n", featureID, featureCmd.featSetGetValue);
        }
    }
    printf("===============================\n");

#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

static int nvme_Print_Arbitration_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ARBITRATION_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tArbitration & Command Processing Feature\n");
        printf("=============================================\n");
        printf("Hi  Priority Weight (HPW) :\t\t0x%02X\n", ((featureCmd.featSetGetValue & 0xFF000000) >> 24));
        printf("Med Priority Weight (MPW) :\t\t0x%02X\n", ((featureCmd.featSetGetValue & 0x00FF0000) >> 16));
        printf("Low Priority Weight (LPW) :\t\t0x%02X\n", ((featureCmd.featSetGetValue & 0x0000FF00) >> 8));
        printf("Arbitration Burst    (AB) :\t\t0x%02X\n", featureCmd.featSetGetValue & 0x00000003);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}


//Temperature Threshold 
static int nvme_Print_Temperature_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
    uint8_t   TMPSEL = 0; //0=Composite, 1=Sensor 1, 2=Sensor 2, ...
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_TEMP_THRESH_;
    featureCmd.sel = selectType;
    printf("\n\tTemperature Threshold Feature\n");
    printf("=============================================\n");
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("Composite Temperature : \t0x%04X\tOver  Temp. Threshold\n", \
            (featureCmd.featSetGetValue & 0x000000FF));
    }
    featureCmd.featSetGetValue = BIT20;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("Composite Temperature : \t0x%04X\tUnder Temp. Threshold\n", \
            (featureCmd.featSetGetValue & 0x000000FF));
    }

    for (TMPSEL = 1; TMPSEL <= 8; TMPSEL++)
    {
        featureCmd.featSetGetValue = (TMPSEL << 16);
        ret = nvme_Get_Features(device, &featureCmd);
        if (ret == SUCCESS)
        {
            printf("Temperature Sensor %d  : \t0x%04X\tOver  Temp. Threshold\n", \
                TMPSEL, (featureCmd.featSetGetValue & 0x000000FF));
        }
        //Not get Under Temperature 
        // BIT20 = THSEL 0=Over Temperature Thresh. 1=Under Temperature Thresh. 
        featureCmd.featSetGetValue = C_CAST(uint32_t, (C_CAST(uint32_t, BIT20) | (C_CAST(uint32_t, TMPSEL) << UINT32_C(16))));
        ret = nvme_Get_Features(device, &featureCmd);
        if (ret == SUCCESS)
        {
            printf("Temperature Sensor %d  : \t0x%04X\tUnder Temp. Threshold\n", \
                TMPSEL, (featureCmd.featSetGetValue & 0x000000FF));
        }
    }
    //WARNING: This is just sending back the last sensor ret 
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Power Management
static int nvme_Print_PM_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_POWER_MGMT_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tPower Management Feature Details\n");
        printf("=============================================\n");
        printf("Workload Hint  (WH) :\t\t0x%02X\n", ((featureCmd.featSetGetValue & 0x000000E0) >> 5));
        printf("Power State    (PS) :\t\t0x%02X\n", featureCmd.featSetGetValue & 0x0000001F);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Error Recovery
static int nvme_Print_Error_Recovery_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ERR_RECOVERY_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tError Recovery Feature Details\n");
        printf("=============================================\n");
        printf("Deallocated Logical Block Error (DULBE) :\t\t%s\n", (featureCmd.featSetGetValue & BIT16) ? "Enabled" : "Disabled");
        printf("Time Limited Error Recovery     (TLER)  :\t\t0x%04X\n", featureCmd.featSetGetValue & 0x0000FFFF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Volatile Write Cache Feature. 
static int nvme_Print_WCE_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_VOLATILE_WC_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tVolatile Write Cache Feature Details\n");
        printf("=============================================\n");
        printf("Volatile Write Cache (WCE) :\t\t%s\n", (featureCmd.featSetGetValue & BIT0) ? "Enabled" : "Disabled");

    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Number of Queues Feature 
static int nvme_Print_NumberOfQueues_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_NUM_QUEUES_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tNumber of Queues Feature Details\n");
        printf("=============================================\n");
        printf("# of I/O Completion Queues Requested (NCQR)  :\t\t0x%04X\n", (featureCmd.featSetGetValue & 0xFFFF0000) >> 16);
        printf("# of I/O Submission Queues Requested (NSQR)  :\t\t0x%04X\n", featureCmd.featSetGetValue & 0x0000FFFF);

        //TODO: How to get NCQA??? Seems like Linux driver limitation? -X
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Interrupt Coalescing (08h Feature)
static int nvme_Print_Intr_Coalescing_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_IRQ_COALESCE_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tInterrupt Coalescing Feature Details\n");
        printf("=============================================\n");
        printf("Aggregation Time     (TIME)  :\t\t0x%02X\n", (featureCmd.featSetGetValue & 0x0000FF00) >> 8);
        printf("Aggregation Threshold (THR)  :\t\t0x%02X\n", featureCmd.featSetGetValue & 0x000000FF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Interrupt Vector Configuration (09h Feature)
static int nvme_Print_Intr_Config_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_IRQ_CONFIG_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tInterrupt Vector Configuration Feature Details\n");
        printf("=============================================\n");
        printf("Coalescing Disable (CD) :\t%s\n", (featureCmd.featSetGetValue & BIT16) ? "Enabled" : "Disabled");
        printf("Interrupt Vector   (IV) :\t0x%02X\n", featureCmd.featSetGetValue & 0x000000FF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Write Atomicity Normal (0Ah Feature)
static int nvme_Print_Write_Atomicity_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_WRITE_ATOMIC_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tWrite Atomicity Normal Feature Details\n");
        printf("=============================================\n");
        printf("Disable Normal (DN) :\t%s\n\n", (featureCmd.featSetGetValue & BIT0) ? "Enabled" : "Disabled");
        if (featureCmd.featSetGetValue & BIT0)
        {
            printf(" Host specifies that AWUN & NAWUN are not required\n");
            printf(" & controller shall only honor AWUPF & NAWUPF\n");
        }
        else
        {
            printf("Controller honors AWUN, NAWUN, AWUPF & NAWUPF\n");
        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Asynchronous Event Configuration (0Bh Feature)
static int nvme_Print_Async_Config_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ASYNC_EVENT_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if (ret == SUCCESS)
    {
        printf("\n\tAsync Event Configuration\n");
        printf("=============================================\n");
        printf("Firmware Activation Notices     :\t%s\n", (featureCmd.featSetGetValue & BIT9) ? "Enabled" : "Disabled");
        printf("Namespace Attribute Notices     :\t%s\n", (featureCmd.featSetGetValue & BIT8) ? "Enabled" : "Disabled");
        printf("SMART/Health Critical Warnings  :\t0x%02X\n", featureCmd.featSetGetValue & 0x000000FF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_Feature_Details(tDevice *device, uint8_t featureID, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    switch (featureID)
    {
    case NVME_FEAT_ARBITRATION_:
        ret = nvme_Print_Arbitration_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_POWER_MGMT_:
        ret = nvme_Print_PM_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_TEMP_THRESH_:
        ret = nvme_Print_Temperature_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_ERR_RECOVERY_:
        ret = nvme_Print_Error_Recovery_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_VOLATILE_WC_:
        ret = nvme_Print_WCE_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_NUM_QUEUES_:
        ret = nvme_Print_NumberOfQueues_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_IRQ_COALESCE_:
        ret = nvme_Print_Intr_Coalescing_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_IRQ_CONFIG_:
        ret = nvme_Print_Intr_Config_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_WRITE_ATOMIC_:
        ret = nvme_Print_Write_Atomicity_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_ASYNC_EVENT_:
        ret = nvme_Print_Async_Config_Feature_Details(device, selectType);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_Log_Size(tDevice *device, uint8_t logPageId, uint64_t * logSize)
{
    int ret = SUCCESS;
    if (logSize)
    {
        *logSize = UINT64_C(0);//make sure this is initialized to zero as some logs below may require calculating the size by reading info from the drive
        switch (logPageId)
        {
        case NVME_LOG_FETURE_IDENTIFIERS_SUPPORTED_AND_EFFECTS_ID:
        case NVME_LOG_SUPPORTED_PAGES_ID:
            *logSize = UINT64_C(1024);
            break;
        case NVME_LOG_ERROR_ID:
            *logSize = UINT64_C(64) * C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.elpe);
            break;
        case NVME_LOG_SMART_ID:
        case NVME_LOG_FW_SLOT_ID:
        case NVME_LOG_ENDURANCE_GROUP_INFO_ID:
        case NVME_LOG_PREDICTABLE_LATENCY_PER_NVM_SET_ID:
        case NVME_LOG_COMMAND_AND_FEATURE_LOCKDOWN_ID:
        case NVME_LOG_ROTATIONAL_MEDIA_INFORMATION_ID:
        case NVME_LOG_SANITIZE_ID:
            *logSize = UINT64_C(512);
            break;
        case NVME_LOG_MN_COMMANDS_SUPPORTED_AND_EFFECTS_ID:
        case NVME_LOG_CHANGED_NAMESPACE_LIST://up to 1024 entries of namespaces...just returning the maximum size
        case NVME_LOG_CMD_SPT_EFET_ID:
            *logSize = UINT64_C(4096);
            break;
        case NVME_LOG_DEV_SELF_TEST_ID:
            *logSize = UINT64_C(564);
            break;
        case NVME_LOG_TELEMETRY_HOST_ID:
        case NVME_LOG_TELEMETRY_CTRL_ID:
            //TODO: do we want to read the first 512 B to determine maximum size???
            *logSize = UINT64_C(0);
            break;
        case NVME_LOG_PREDICTABLE_LATENCY_EVENT_AGREGATE_ID:
            *logSize = UINT64_C(8) + (UINT64_C(2) * C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.nsetidmax));
            break;
        case NVME_LOG_ASYMMETRIC_NAMESPACE_ACCESS_ID:
            //ANAGRPMAX for maximum value
            //NANAGRPID for number of ANA groups supported by the controller
            //16B header for the log
            //each group descriptor is 32B + (number of NSIDs * 4) B in length
            //So the maximum size this log could be is calculated in this section as:
            //16 + (ANAGRPMAX * (32 + (ctrlNN * 4)))
            //TODO: change this to use current number of namespaces or NANAGRPID instead to return a smaller size
            //TODO: Or read header and use that info to calculate the size
            *logSize = UINT64_C(16) + (C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.anagrpmax) * (UINT64_C(32) + (C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.nn) * UINT64_C(4))));
            break;
        case NVME_LOG_PERSISTENT_EVENT_LOG_ID:
            //512B header, each event is24B + vendor specific info (max 65535B) + event data
            //PELS is maximum persistent events in 64KiB units
            *logSize = C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.pels) * UINT64_C(65536);
            break;
        case NVME_LOG_ENDURANCE_GROUP_EVENT_AGREGATE_ID:
            *logSize = UINT64_C(8) + (C_CAST(uint64_t, device->drive_info.IdentifyData.nvme.ctrl.endgidmax) * UINT64_C(2));
            break;
        case NVME_LOG_MEDIA_UNIT_STATUS_ID:
            //TODO: Read the first 16B to get NMU and CCHANS to figure out the total size
            //      Selected Configuration will play a part in the size calculation as well. If zero, MUCS in each descriptor will be zero
            *logSize = UINT64_C(0);
            break;
        case NVME_LOG_SUPPORTED_CAPACITY_CONFIGURATION_LIST_ID:
            //TODO: read 16B header to get SCCN. NOTE: configuration descriptors can be different lengths...this is due to number of endurance groups accessible by the controller.
            //Each is variable by number of endurance groups, number NVM sets, number of channels and number of media units. Talk about complicated.
            *logSize = UINT64_C(0);
            break;
        case NVME_LOG_BOOT_PARTITION_ID:
            //TODO: read 16B header to get boot partition data size and calculate this
            *logSize = UINT64_C(0);
            break;
        case NVME_LOG_DISCOVERY_ID:
            //TODO: read 1024B header ang get number of records, then multiply by 1024 to get size (adding 1024B header)
            *logSize = UINT64_C(0);
            break;
        case NVME_LOG_RESERVATION_ID:
            *logSize = UINT64_C(64);
            break;
        case NVME_LOG_COMMAND_SET_SPECIFIC_ID:
        default:
            //Unknown log ID
            //TODO: Set ret to something else for unknown size???
            *logSize = UINT64_C(0);
            break;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

int nvme_Print_FWSLOTS_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    int slot = 0;
    nvmeFirmwareSlotInfo fwSlotsLogInfo;
    char fwRev[9]; // 8 bytes for the FSR + 1 byte '\0'
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    memset(&fwSlotsLogInfo, 0, sizeof(nvmeFirmwareSlotInfo));
    ret = nvme_Get_FWSLOTS_Log_Page(device, (uint8_t*)&fwSlotsLogInfo, sizeof(nvmeFirmwareSlotInfo));
    if (ret == SUCCESS)
    {
#ifdef _DEBUG
        printf("AFI: 0x%X\n", fwSlotsLogInfo.afi);
#endif
        printf("\nFirmware slot actively running firmware: %d\n", fwSlotsLogInfo.afi & 0x03);
        printf("Firmware slot to be activated at next reset: %d\n\n", ((fwSlotsLogInfo.afi & 0x30) >> 4));

        for (slot = 1; slot <= NVME_MAX_FW_SLOTS; slot++)
        {
            memcpy(fwRev, (char *)&fwSlotsLogInfo.FSR[slot - 1], 8);
            fwRev[8] = '\0';
            printf(" Slot %d : %s\n", slot, fwRev);
        }
    }

#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

void show_effects_log_human(uint32_t effect)
{
    const char *set = "+";
    const char *clr = "-";

    printf("  CSUPP+");
    printf("  LBCC%s", (effect & NVME_CMD_EFFECTS_LBCC) ? set : clr);
    printf("  NCC%s", (effect & NVME_CMD_EFFECTS_NCC) ? set : clr);
    printf("  NIC%s", (effect & NVME_CMD_EFFECTS_NIC) ? set : clr);
    printf("  CCC%s", (effect & NVME_CMD_EFFECTS_CCC) ? set : clr);

    if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 0)
        printf("  No command restriction\n");
    else if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 1)
        printf("  No other command for same namespace\n");
    else if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 2)
        printf("  No other command for any namespace\n");
    else
        printf("  Reserved CSE\n");
}

int nvme_Print_CmdSptEfft_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    nvmeEffectsLog effectsLogInfo;
    uint16_t i = 0;
    uint32_t effect = 0;

#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif

    memset(&effectsLogInfo, 0, sizeof(nvmeEffectsLog));
    ret = nvme_Get_CmdSptEfft_Log_Page(device, (uint8_t*)&effectsLogInfo, sizeof(nvmeEffectsLog));
    if (ret == SUCCESS)
    {
        printf("Admin Command Set\n");
        for (i = 0; i < 256; i++)
        {
            effect = effectsLogInfo.acs[i];
            if (effect & NVME_CMD_EFFECTS_CSUPP)
            {
                printf("ACS%-6" PRIu16 "[%-32s] %08" PRIX32, i, nvme_cmd_to_string(1, C_CAST(uint8_t, i)), effect);
                show_effects_log_human(effect);
            }
        }
        printf("\nNVM Command Set\n");
        for (i = 0; i < 256; i++)
        {
            effect = effectsLogInfo.iocs[i];
            if (effect & NVME_CMD_EFFECTS_CSUPP)
            {
                printf("IOCS%-5" PRIu16 "[%-32s] %08" PRIX32, i, nvme_cmd_to_string(0, C_CAST(uint8_t, i)), effect);
                show_effects_log_human(effect);
            }
        }
    }

#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}


int nvme_Print_DevSelfTest_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    nvmeSelfTestLog selfTestLogInfo;
    int i, temp;
    const char *test_code_res;
    const char *test_res[10] = {
        "Operation completed without error",
        "Operation was aborted by a Device Self-test command",
        "Operation was aborted by a Controller Level Reset",
        "Operation was aborted due to a removal of a namespace from the namespace inventory",
        "Operation was aborted due to the processing of a Format NVM command",
        "A fatal error or unknown test error occurred while the controller was executing the"\
        " device self-test operation andthe operation did not complete",
        "Operation completed with a segment that failed and the segment that failed is not known",
        "Operation completed with one or more failed segments and the first segment that failed "\
        "is indicated in the SegmentNumber field",
        "Operation was aborted for unknown reason",
        "Reserved"
    };


#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif

    memset(&selfTestLogInfo, 0, sizeof(nvmeSelfTestLog));
    ret = nvme_Get_DevSelfTest_Log_Page(device, (uint8_t*)&selfTestLogInfo, sizeof(nvmeSelfTestLog));
    if (ret == SUCCESS)
    {
        printf("Current operation : %#x\n", selfTestLogInfo.crntDevSelftestOprn);
        printf("Current Completion : %u%%\n", selfTestLogInfo.crntDevSelftestCompln);
        for (i = 0; i < NVME_SELF_TEST_REPORTS; i++)
        {
            temp = selfTestLogInfo.result[i].deviceSelfTestStatus & 0xf;
            if (temp == 0xf)
                continue;

            printf("Result[%d]:\n", i);
            printf("  Test Result                  : %#x %s\n", temp,
                test_res[temp > 9 ? 9 : temp]);

            temp = selfTestLogInfo.result[i].deviceSelfTestStatus >> 4;
            switch (temp) {
            case 1:
                test_code_res = "Short device self-test operation";
                break;
            case 2:
                test_code_res = "Extended device self-test operation";
                break;
            case 0xe:
                test_code_res = "Vendor specific";
                break;
            default:
                test_code_res = "Reserved";
                break;
            }
            printf("  Test Code                    : %#x %s\n", temp,
                test_code_res);
            if (temp == 7)
                printf("  Segment number               : %#x\n",
                    selfTestLogInfo.result[i].segmentNum);

            temp = selfTestLogInfo.result[i].validDiagnosticInfo;
            printf("  Valid Diagnostic Information : %#x\n", temp);
            printf("  Power on hours (POH)         : %#"PRIx64"\n", selfTestLogInfo.result[i].powerOnHours);

            if (temp & NVME_SELF_TEST_VALID_NSID)
                printf("  Namespace Identifier         : %#x\n", selfTestLogInfo.result[i].nsid);

            if (temp & NVME_SELF_TEST_VALID_FLBA)
                printf("  Failing LBA                  : %#"PRIx64"\n", selfTestLogInfo.result[i].failingLba);

            if (temp & NVME_SELF_TEST_VALID_SCT)
                printf("  Status Code Type             : %#x\n", selfTestLogInfo.result[i].statusCodeType);

            if (temp & NVME_SELF_TEST_VALID_SC)
                printf("  Status Code                  : %#x\n", selfTestLogInfo.result[i].statusCode);

            printf("  Vendor Specific                      : %x %x\n", selfTestLogInfo.result[i].vendorSpecific[0], selfTestLogInfo.result[i].vendorSpecific[1]);
        }
    }

#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_ERROR_Log_Page(tDevice *device, uint64_t numOfErrToPrint)
{
    int ret = UNKNOWN;
    int err = 0;
    nvmeErrLogEntry * pErrLogBuf = NULL;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    //TODO: If this is not specified get the value. 
    if (!numOfErrToPrint)
    {
        numOfErrToPrint = 32;
    }
    pErrLogBuf = C_CAST(nvmeErrLogEntry *, calloc_aligned(C_CAST(size_t, numOfErrToPrint), sizeof(nvmeErrLogEntry), device->os_info.minimumAlignment));
    if (pErrLogBuf != NULL)
    {
        ret = nvme_Get_ERROR_Log_Page(device, C_CAST(uint8_t*, pErrLogBuf), C_CAST(uint32_t, numOfErrToPrint * sizeof(nvmeErrLogEntry)));
        if (ret == SUCCESS)
        {
            printf("Err #\tLBA\t\tSQ ID\tCMD ID\tStatus\tLocation\n");
            printf("=======================================================\n");
            for (err = 0; err < C_CAST(int, numOfErrToPrint); err++)
            {
                if (pErrLogBuf[err].errorCount)
                {

                    printf("%"PRIu64"\t%"PRIu64"\t%"PRIu16"\t%"PRIu16"\t0x%02"PRIX16"\t0x%02"PRIX16"\n", \
                        pErrLogBuf[err].errorCount,
                        pErrLogBuf[err].lba,
                        pErrLogBuf[err].subQueueID,
                        pErrLogBuf[err].cmdID,
                        pErrLogBuf[err].statusField,
                        pErrLogBuf[err].paramErrLocation);
                }
            }
        }
    }
    safe_Free_aligned(pErrLogBuf)
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

int print_Nvme_Ctrl_Regs(tDevice * device)
{
    int ret = UNKNOWN;

    nvmeBarCtrlRegisters ctrlRegs;

    memset(&ctrlRegs, 0, sizeof(nvmeBarCtrlRegisters));

    printf("\n=====CONTROLLER REGISTERS=====\n");

    ret = nvme_Read_Ctrl_Reg(device, &ctrlRegs);

    if (ret == SUCCESS)
    {
        printf("Controller Capabilities (CAP)\t:\t%"PRIx64"\n", ctrlRegs.cap);
        printf("Version (VS)\t:\t0x%x\n", ctrlRegs.vs);
        printf("Interrupt Mask Set (INTMS)\t:\t0x%x\n", ctrlRegs.intms);
        printf("Interrupt Mask Clear (INTMC)\t:\t0x%x\n", ctrlRegs.intmc);
        printf("Controller Configuration (CC)\t:\t0x%x\n", ctrlRegs.cc);
        printf("Controller Status (CSTS)\t:\t0x%x\n", ctrlRegs.csts);
        //What about NSSR?
        printf("Admin Queue Attributes (AQA)\t:\t0x%x\n", ctrlRegs.aqa);
        printf("Admin Submission Queue Base Address (ASQ)\t:\t%"PRIx64"\n", ctrlRegs.asq);
        printf("Admin Completion Queue Base Address (ACQ)\t:\t%"PRIx64"\n", ctrlRegs.acq);
    }
    else
    {
        printf("Couldn't read Controller register for dev %s\n", device->os_info.name);
    }
    return ret;
}
