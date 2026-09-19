/*
* This source file is part of the EtherCAT Slave Stack Code licensed by
* Beckhoff Automation GmbH & Co KG, 33415 Verl, Germany.
* The corresponding license agreement applies. This hint shall not be removed.
* https://www.beckhoff.com/media/downloads/slave-stack-code/ethercat_ssc_license.pdf
*/

/**
\addtogroup CiA402appl CiA402 Sample Application
@{
*/

/**
\file cia402appl.c
\author EthercatSSC@beckhoff.com
\brief Implementation
This file contains all ciA402 specific functions

\version 5.13

<br>Changes to version V5.12:<br>
V5.13 CIA402 2: write profile info to 0xF010.x bit15-0 (was bit16-31 before)<br>
V5.13 CIA402 3: change define "CIA402_DEVICE" to "CiA402_SAMPLE_APPLICATION"<br>
<br>Changes to version V5.11:<br>
V5.12 COE3: update entry access right handling<br>
<br>Changes to version V5.10:<br>
V5.11 ECAT11: create application interface function pointer,<br>
add eeprom emulation interface functions<br>
<br>Changes to version V5.01:<br>
V5.10 CIA402 1: Update complete access handling for 0xF030<br>
V5.10 ECAT6: Add "USE_DEFAULT_MAIN" to enable or disable the main function<br>
<br>Changes to version V5.0:<br>
V5.01 ESC2: Add missed value swapping<br>
<br>Changes to version V4.40:<br>
V5.0 CIA402 1: Syntax bugfix in dummy motion controller<br>
V5.0 CIA402 2: Handle 0xF030/0xF050 in correlation do PDO assign/mapping objects<br>
V5.0 CIA402 3: Trigger dummy motion controller if valid mode of operation is set.<br>
V5.0 CIA402 4: Change Axes structure handling and resources allocation.<br>
V5.0 ECAT2: Create generic application interface functions.<br>
Documentation in Application Note ET9300.<br>
<br>Changes to version V4.30:<br>
V4.40 CoE 6: add AL Status code to Init functions<br>
V4.40 CIA402 2: set motion control trigger depending on synchronisation,<br>
mode of operation and cycle time<br>
V4.40 CIA402 1: change behaviour and name of status-word bit 12<br>
(WG CIA402 24.02.2010)<br>
V4.30 : create file (state machine; handling state transition options; input feedback)
*/

/*-----------------------------------------------------------------------------------------
------
------    Includes
------
-----------------------------------------------------------------------------------------*/
#include "ecat_def.h"

/*ECATCHANGE_START(V5.13) CIA402 3*/
/*ECATCHANGE_END(V5.13) CIA402 3*/

#include "applInterface.h"


#include "coeappl.h"

#define _CiA402_
#include "cia402appl.h"
#undef _CiA402_


/*--------------------------------------------------------------------------------------
------
------    local types and defines
------
--------------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------------
------
------    local variables and constants
------
-----------------------------------------------------------------------------------------*/
TCiA402Axis       LocalAxes[MAX_AXES];

/*-----------------------------------------------------------------------------------------
------
------    application specific functions
------
-----------------------------------------------------------------------------------------*/

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    0               Init CiA402 device successful
            ALSTATUSCODE_XX Init CiA402 device failed

 \brief    This function initializes the Axes structures
*////////////////////////////////////////////////////////////////////////////////////////
/**************************************************************
 * CiA402 Axis Object Dictionary Static Storage
 *
 * 说明：
 * 原来部分 SSC 示例可能使用 ALLOCMEM / malloc 给每个轴动态申请对象字典。
 * 这里改成静态数组，避免在单片机工程中使用动态内存。
 *
 * 注意：
 * 这段代码必须放在 DefCiA402AxisObjDic 定义之后，
 * 因为这里要用 SIZEOF(DefCiA402AxisObjDic) 计算对象字典表的数量。
 **************************************************************/

#define CIA402_AXIS_OBJ_DIC_ENTRY_NUM   (SIZEOF(DefCiA402AxisObjDic) / SIZEOF(TOBJECT))

static TOBJECT OBJMEM CiA402AxisObjDicStorage[MAX_AXES][CIA402_AXIS_OBJ_DIC_ENTRY_NUM];

/**
 * @brief Validate one PDO assignment and calculate its mapped bit length.
 * @param[in] pdoIndex PDO mapping-object index.
 * @param[in] txPdo TRUE for TxPDO mapping, FALSE for RxPDO mapping.
 * @param[out] pSizeBits Receives the total mapped size in bits.
 * @return ALSTATUSCODE_NOERROR or the corresponding invalid-mapping code.
 */
static UINT16 CiA402_GetPdoSizeBits(UINT16 pdoIndex, BOOL txPdo, UINT16 *pSizeBits)
{
    OBJCONST TOBJECT OBJMEM *pPdo;
    UINT16 entryCount;
    UINT16 entry;
    UINT16 sizeBits = 0U;

    if ((txPdo && !IS_TX_PDO(pdoIndex)) || (!txPdo && !IS_RX_PDO(pdoIndex)))
    {
        return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
    }

    pPdo = OBJ_GetObjectHandle(pdoIndex);
    if ((pPdo == NULL) || (pPdo->pVarPtr == NULL))
    {
        return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
    }

    entryCount = *((UINT16 *)pPdo->pVarPtr);
    for (entry = 1U; entry <= entryCount; entry++)
    {
        UINT32 mapping;
        UINT16 objectIndex;
        UINT8 subIndex;
        UINT8 bitLength;
        OBJCONST TOBJECT OBJMEM *pObject;
        OBJCONST TSDOINFOENTRYDESC OBJMEM *pEntryDesc;
        UINT16 requiredAccess;
        UINT16 maxSubIndex;

        mapping = *((UINT32 *)((UINT8 *)pPdo->pVarPtr
                    + (OBJ_GetEntryOffset((UINT8)entry, pPdo) >> 3U)));
        objectIndex = (UINT16)(mapping >> 16U);
        subIndex = (UINT8)(mapping >> 8U);
        bitLength = (UINT8)mapping;

        if ((bitLength == 0U) || ((bitLength & 0x07U) != 0U))
        {
            return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
        }

        sizeBits = (UINT16)(sizeBits + bitLength);
        if (objectIndex == 0U)
        {
            continue;
        }

        pObject = OBJ_GetObjectHandle(objectIndex);
        if ((pObject == NULL) || (pObject->pVarPtr == NULL))
        {
            return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
        }

        maxSubIndex = (pObject->ObjDesc.ObjFlags & OBJFLAGS_MAXSUBINDEXMASK)
                      >> OBJFLAGS_MAXSUBINDEXSHIFT;
        if (subIndex > maxSubIndex)
        {
            return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
        }

        pEntryDesc = OBJ_GetEntryDesc(pObject, subIndex);
        requiredAccess = txPdo ? OBJACCESS_TXPDOMAPPING : OBJACCESS_RXPDOMAPPING;
        if ((pEntryDesc == NULL)
            || (pEntryDesc->BitLength != bitLength)
            || ((pEntryDesc->ObjAccess & requiredAccess) == 0U)
            || ((OBJ_GetEntryOffset(subIndex, pObject) & 0x07U) != 0U))
        {
            return txPdo ? ALSTATUSCODE_INVALIDINPUTMAPPING : ALSTATUSCODE_INVALIDOUTPUTMAPPING;
        }
    }

    *pSizeBits = sizeBits;
    return ALSTATUSCODE_NOERROR;
}

/**
 * @brief Copy all byte-aligned entries described by one PDO mapping object.
 * @param[in] pdoIndex PDO mapping-object index.
 * @param[in,out] pProcessData EtherCAT process-data buffer.
 * @param[in] toProcessData TRUE copies object values to Tx process data;
 *                          FALSE copies Rx process data into object values.
 * @param[in,out] pProcessOffsetBytes Current process-data byte offset, advanced
 *                                    by every mapped entry including padding.
 */
static void CiA402_CopyPdo(UINT16 pdoIndex, UINT8 *pProcessData, BOOL toProcessData,
                           UINT16 *pProcessOffsetBytes)
{
    OBJCONST TOBJECT OBJMEM *pPdo;
    UINT16 entryCount;
    UINT16 entry;

    pPdo = OBJ_GetObjectHandle(pdoIndex);
    if ((pPdo == NULL) || (pPdo->pVarPtr == NULL))
    {
        return;
    }

    entryCount = *((UINT16 *)pPdo->pVarPtr);
    for (entry = 1U; entry <= entryCount; entry++)
    {
        UINT32 mapping;
        UINT16 objectIndex;
        UINT8 subIndex;
        UINT8 byteLength;

        mapping = *((UINT32 *)((UINT8 *)pPdo->pVarPtr
                    + (OBJ_GetEntryOffset((UINT8)entry, pPdo) >> 3U)));
        objectIndex = (UINT16)(mapping >> 16U);
        subIndex = (UINT8)(mapping >> 8U);
        byteLength = (UINT8)((mapping & 0xFFU) >> 3U);

        if (objectIndex == 0U)
        {
            if (toProcessData)
            {
                HMEMSET(&pProcessData[*pProcessOffsetBytes], 0, byteLength);
            }
        }
        else
        {
            OBJCONST TOBJECT OBJMEM *pObject;
            UINT8 *pObjectData;

            pObject = OBJ_GetObjectHandle(objectIndex);
            if ((pObject != NULL) && (pObject->pVarPtr != NULL))
            {
                pObjectData = (UINT8 *)pObject->pVarPtr
                              + (OBJ_GetEntryOffset(subIndex, pObject) >> 3U);
                if (toProcessData)
                {
                    MEMCPY(&pProcessData[*pProcessOffsetBytes], pObjectData, byteLength);
                }
                else
                {
                    MEMCPY(pObjectData, &pProcessData[*pProcessOffsetBytes], byteLength);
                }
            }
        }

        *pProcessOffsetBytes = (UINT16)(*pProcessOffsetBytes + byteLength);
    }
}


/**
 * @brief 分配并初始化全部 CiA402 轴对象。
 * @return 成功返回 ALSTATUSCODE_NOERROR，否则返回对象字典初始化错误码。
 */
UINT16 CiA402_Init(void)
{
    UINT16 result = 0;
    UINT16 AxisCnt = 0;
    UINT16 j = 0;
    UINT32 ObjectOffset = 0x800;
    UINT8 PDOOffset = 0x10;

    /* 每个轴分别建立状态机、对象字典和 PDO 映射。 */
    for (AxisCnt = 0; AxisCnt < MAX_AXES; AxisCnt++)
    {
        TOBJECT OBJMEM *pDiCEntry = NULL;

        /* 清空轴运行状态，并设置为“未准备好上电”的安全初始状态。 */
        HMEMSET(&LocalAxes[AxisCnt], 0, SIZEOF(TCiA402Axis));

        LocalAxes[AxisCnt].bAxisIsActive = FALSE;
        LocalAxes[AxisCnt].bBrakeApplied = TRUE;
        LocalAxes[AxisCnt].bLowLevelPowerApplied = TRUE;
        LocalAxes[AxisCnt].bHighLevelPowerApplied = FALSE;
        LocalAxes[AxisCnt].bAxisFunctionEnabled = FALSE;
        LocalAxes[AxisCnt].bConfigurationAllowed = TRUE;

        LocalAxes[AxisCnt].i16State = STATE_NOT_READY_TO_SWITCH_ON;
        LocalAxes[AxisCnt].u16PendingOptionCode = 0x00;

        LocalAxes[AxisCnt].fCurPosition = 0;
        /* SM 同步默认按 1 ms 主站周期运行；使用 DC 时会替换为实际 Sync0 周期。 */
        LocalAxes[AxisCnt].u32CycleTime = 1000U;

        /***********************************
         * init objects
         ***********************************/

        /* 从默认模板复制 0x6040、0x6041、0x6060 等 CiA402 对象初值。 */
        HMEMCPY(&LocalAxes[AxisCnt].Objects,
                &DefCiA402ObjectValues,
                CIA402_OBJECTS_SIZE);

        /***********************************
         * set Object offset to PDO entries
         ***********************************/

        /* 根据轴号修正 RxPDO/TxPDO 中的对象索引，单轴时偏移为 0。 */
        /* csv/csp RxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sRxPDOMap0.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sRxPDOMap0.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /* csp RxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sRxPDOMap1.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sRxPDOMap1.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /* csv RxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sRxPDOMap2.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sRxPDOMap2.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /* csv/csp TxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sTxPDOMap0.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sTxPDOMap0.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /* csp TxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sTxPDOMap1.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sTxPDOMap1.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /* csv TxPDO */
        for (j = 0; j < LocalAxes[AxisCnt].Objects.sTxPDOMap2.u16SubIndex0; j++)
        {
            LocalAxes[AxisCnt].Objects.sTxPDOMap2.aEntries[j] += AxisCnt * (ObjectOffset << 16);
        }

        /***********************************
         * init objects dictionary entries
         * 使用静态数组，不再使用 ALLOCMEM / malloc
         ***********************************/

        LocalAxes[AxisCnt].ObjDic = &CiA402AxisObjDicStorage[AxisCnt][0];

        HMEMCPY(LocalAxes[AxisCnt].ObjDic,
                &DefCiA402AxisObjDic,
                SIZEOF(DefCiA402AxisObjDic));

        pDiCEntry = LocalAxes[AxisCnt].ObjDic;

        /***********************************
         * adapt Object index and assign Var pointer
         ***********************************/
        while (pDiCEntry->Index != 0xFFFF)
        {
            BOOL bObjectFound = TRUE;

            switch (pDiCEntry->Index)
            {
                case 0x1600:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sRxPDOMap0;
                    break;

                case 0x1601:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sRxPDOMap1;
                    break;

                case 0x1602:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sRxPDOMap2;
                    break;

                case 0x1A00:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sTxPDOMap0;
                    break;

                case 0x1A01:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sTxPDOMap1;
                    break;

                case 0x1A02:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.sTxPDOMap2;
                    break;

                case 0x603F:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objErrorCode;
                    break;

                case 0x6040:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objControlWord;
                    break;

                case 0x6041:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objStatusWord;
                    break;

                case 0x605A:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objQuickStopOptionCode;
                    break;

                case 0x605B:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objShutdownOptionCode;
                    break;

                case 0x605C:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objDisableOperationOptionCode;
                    break;

                case 0x605E:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objFaultReactionCode;
                    break;

                case 0x6060:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objModesOfOperation;
                    break;

                case 0x6061:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objModesOfOperationDisplay;
                    break;

                case 0x6064:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objPositionActualValue;
                    break;

                case 0x606C:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objVelocityActualValue;
                    break;

                case 0x6077:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objTorqueActualValue;
                    break;

                case 0x607A:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objTargetPosition;
                    break;

                case 0x607D:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objSoftwarePositionLimit;
                    break;

                case 0x6085:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objQuickStopDeclaration;
                    break;

                case 0x60C2:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objInterpolationTimePeriod;
                    break;

                case 0x60FF:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objTargetVelocity;
                    break;

                case 0x6502:
                    pDiCEntry->pVarPtr = &LocalAxes[AxisCnt].Objects.objSupportedDriveModes;
                    break;

                default:
                    bObjectFound = FALSE;
                    break;
            }

            /*
             * 这里暂时不处理 bObjectFound。
             * 有些对象可能不需要单独绑定 pVarPtr。
             * 如果后面调试对象字典异常，可以在这里加断点观察。
             */
            (void)bObjectFound;

            /***********************************
             * increment object index
             *
             * PDO 映射相关对象：
             * 0x1600、0x1A00 等，每个轴偏移 0x10
             *
             * CiA402 参数对象：
             * 0x6040、0x6060、0x607A 等，每个轴偏移 0x800
             ***********************************/
            if (pDiCEntry->Index >= 0x1400 && pDiCEntry->Index <= 0x1BFF)
            {
                pDiCEntry->Index += AxisCnt * PDOOffset;
            }
            else
            {
                pDiCEntry->Index += AxisCnt * (UINT16)ObjectOffset;
            }

            pDiCEntry++;
        }
    }

    return result;
}
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    CiA402_DeallocateAxis
 \brief    Remove all allocated axes resources
*////////////////////////////////////////////////////////////////////////////////////////
void CiA402_DeallocateAxis(void)
{
    UINT8 cnt = 0;

    for(cnt = 0 ; cnt < MAX_AXES ; cnt++)
    {
    /*Remove object dictionary entries*/
    if(LocalAxes[cnt].ObjDic != NULL)
    {
        TOBJECT OBJMEM *pEntry = LocalAxes[cnt].ObjDic;

        while(pEntry->Index != 0xFFFF)
        {
            COE_RemoveDicEntry(pEntry->Index);

            pEntry++;
        }

        LocalAxes[cnt].ObjDic = NULL;
    }

    nPdOutputSize = 0;
    nPdInputSize = 0;

    }

}
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    CiA402-Statemachine
        This function handles the state machine for devices using the CiA402 profile.
        called cyclic from MainLoop()
        All described transition numbers are referring to the document
        "ETG Implementation Guideline for the CiA402 Axis Profile"
        located on the EtherCAT.org download section

*////////////////////////////////////////////////////////////////////////////////////////
static BOOL CiA402_CommandIs(UINT16 control_word,
                            UINT16 mask,
                            UINT16 command)
{
    /* 只比较该命令关心的位，忽略控制字中的其他模式相关位。 */
    return (BOOL)((control_word & mask) == command);
}

/**
 * @brief 启动需要时间完成的停止动作。
 *
 * option_code为0时直接切换到next_state；否则保存0x605A～0x605E
 * 对象编号，等待CiA402_Application()执行减速或故障处理。
 */
static BOOL CiA402_StartOption(TCiA402Axis *axis,
                               INT16 option_code,
                               UINT16 pending_code,
                               INT16 next_state)
{
    if (option_code == DISABLE_DRIVE)
    {
        axis->i16State = next_state;
        return TRUE;
    }

    axis->u16PendingOptionCode = pending_code;
    return FALSE;
}

/**
 * @brief Not Ready状态处理。
 *
 * EtherCAT进入OP后执行自动转换：
 *   Not Ready(内部0x0001) -> Switch On Disabled(内部0x0002)。
 * 当前CiA402_StateMachine()还有OP快捷处理，因此通常会直接进入Ready。
 */
static void CiA402_HandleNotReady(TCiA402Axis *axis)
{
    if (nAlStatus == STATE_OP)
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
    }
}

/**
 * @brief Switch On Disabled状态处理。
 *
 * 控制字0x0006：
 *   Switch On Disabled -> Ready To Switch On。
 * 稳定后的0x6041状态位为0x0021。
 */
static void CiA402_HandleSwitchOnDisabled(TCiA402Axis *axis,
                                          UINT16 control_word)
{
    if (CiA402_CommandIs(control_word,
                         CONTROLWORD_COMMAND_SHUTDOWN_MASK,
                         CONTROLWORD_COMMAND_SHUTDOWN))
    {
        axis->i16State = STATE_READY_TO_SWITCH_ON;
    }
}

/**
 * @brief Ready To Switch On状态处理。
 *
 * 控制字切换关系：
 *   0x0000或0x0002：Ready -> Switch On Disabled。
 *   0x0006：保持Ready，0x6041状态位保持0x0021。
 *   0x0007或0x000F：Ready -> Switched On。
 *
 * 注意：Ready状态收到0x000F只先进入Switched On，下一轮状态机
 * 仍收到0x000F时，才进入Operation Enabled。
 */
static void CiA402_HandleReadyToSwitchOn(TCiA402Axis *axis,
                                         UINT16 control_word)
{
    BOOL stop_requested;
    BOOL switch_on_requested;

    stop_requested = CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_QUICKSTOP_MASK,
        CONTROLWORD_COMMAND_QUICKSTOP);
    stop_requested |= CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_DISABLEVOLTAGE_MASK,
        CONTROLWORD_COMMAND_DISABLEVOLTAGE);

    if (stop_requested)
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
        return;
    }

    switch_on_requested = CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_SWITCHON_MASK,
        CONTROLWORD_COMMAND_SWITCHON);
    switch_on_requested |= CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_SWITCHON_ENABLEOPERATION_MASK,
        CONTROLWORD_COMMAND_SWITCHON_ENABLEOPERATION);

    if (switch_on_requested)
    {
        axis->i16State = STATE_SWITCHED_ON;
    }
}

/**
 * @brief Switched On状态处理。
 *
 * 控制字切换关系：
 *   0x0006：Switched On -> Ready To Switch On。
 *   0x0000或0x0002：Switched On -> Switch On Disabled。
 *   0x0007：保持Switched On，0x6041状态位保持0x0023。
 *   0x000F：Switched On -> Operation Enabled。
 */
static void CiA402_HandleSwitchedOn(TCiA402Axis *axis,
                                    UINT16 control_word)
{
    BOOL stop_requested;

    if (CiA402_CommandIs(control_word,
                         CONTROLWORD_COMMAND_SHUTDOWN_MASK,
                         CONTROLWORD_COMMAND_SHUTDOWN))
    {
        axis->i16State = STATE_READY_TO_SWITCH_ON;
        return;
    }

    stop_requested = CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_QUICKSTOP_MASK,
        CONTROLWORD_COMMAND_QUICKSTOP);
    stop_requested |= CiA402_CommandIs(
        control_word,
        CONTROLWORD_COMMAND_DISABLEVOLTAGE_MASK,
        CONTROLWORD_COMMAND_DISABLEVOLTAGE);

    if (stop_requested)
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
        return;
    }

    if (CiA402_CommandIs(control_word,
                         CONTROLWORD_COMMAND_ENABLEOPERATION_MASK,
                         CONTROLWORD_COMMAND_ENABLEOPERATION))
    {
        axis->i16State = STATE_OPERATION_ENABLED;
    }
}

/**
 * @brief Operation Enabled状态处理。
 *
 * 控制字切换关系：
 *   0x000F：保持Operation Enabled，0x6041状态位为0x0027。
 *   0x0007：执行0x605C停止选项，再进入Switched On。
 *   0x0006：执行0x605B停止选项，再进入Ready To Switch On。
 *   0x0002：进入Quick Stop Active。
 *   0x0000：直接进入Switch On Disabled。
 */
static BOOL CiA402_HandleOperationEnabled(TCiA402Axis *axis,
                                          UINT16 control_word)
{
    if (CiA402_CommandIs(
            control_word,
            CONTROLWORD_COMMAND_DISABLEOPERATION_MASK,
            CONTROLWORD_COMMAND_DISABLEOPERATION))
    {
        return CiA402_StartOption(
            axis,
            axis->Objects.objDisableOperationOptionCode,
            0x605CU,
            STATE_SWITCHED_ON);
    }

    if (CiA402_CommandIs(control_word,
                         CONTROLWORD_COMMAND_QUICKSTOP_MASK,
                         CONTROLWORD_COMMAND_QUICKSTOP))
    {
        axis->i16State = STATE_QUICK_STOP_ACTIVE;
        return TRUE;
    }

    if (CiA402_CommandIs(control_word,
                         CONTROLWORD_COMMAND_SHUTDOWN_MASK,
                         CONTROLWORD_COMMAND_SHUTDOWN))
    {
        return CiA402_StartOption(
            axis,
            axis->Objects.objShutdownOptionCode,
            0x605BU,
            STATE_READY_TO_SWITCH_ON);
    }

    if (CiA402_CommandIs(
            control_word,
            CONTROLWORD_COMMAND_DISABLEVOLTAGE_MASK,
            CONTROLWORD_COMMAND_DISABLEVOLTAGE))
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
    }

    return TRUE;
}

/**
 * @brief Quick Stop Active状态处理。
 *
 * 第一次进入时设置待处理对象0x605A，由CiA402_Application()执行
 * 快速停止；控制字0x0000可直接切换到Switch On Disabled。
 */
static BOOL CiA402_HandleQuickStop(TCiA402Axis *axis,
                                   UINT16 control_word)
{
    UINT16 previous_state;

    previous_state = axis->Objects.objStatusWord;
    previous_state &= STATUSWORD_STATE_MASK;

    if ((axis->Objects.objQuickStopOptionCode != DISABLE_DRIVE) &&
        (previous_state != STATUSWORD_STATE_QUICKSTOPACTIVE))
    {
        axis->u16PendingOptionCode = 0x605AU;
        return FALSE;
    }

    if (CiA402_CommandIs(
            control_word,
            CONTROLWORD_COMMAND_DISABLEVOLTAGE_MASK,
            CONTROLWORD_COMMAND_DISABLEVOLTAGE))
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
    }

    return TRUE;
}

/**
 * @brief Fault Reaction Active状态处理。
 *
 * 故障停止选项非0时设置待处理对象0x605E，停止完成后进入Fault；
 * 停止选项为0时直接进入Fault。
 */
static BOOL CiA402_HandleFaultReaction(TCiA402Axis *axis)
{
    if (axis->Objects.objFaultReactionCode != DISABLE_DRIVE)
    {
        axis->u16PendingOptionCode = 0x605EU;
        return FALSE;
    }

    axis->i16State = STATE_FAULT;
    return TRUE;
}

/**
 * @brief Fault状态处理。
 *
 * 控制字Bit7置1，即(control_word & 0x0080) == 0x0080时，
 * 清除0x1001错误码并进入Switch On Disabled。
 */
static void CiA402_HandleFault(TCiA402Axis *axis,
                               UINT16 control_word)
{
    if (!CiA402_CommandIs(control_word,
                          CONTROLWORD_COMMAND_FAULTRESET_MASK,
                          CONTROLWORD_COMMAND_FAULTRESET))
    {
        return;
    }

    axis->Objects.objErrorCode = 0U;
    axis->i16State = STATE_SWITCH_ON_DISABLED;
}

/**
 * @brief 根据当前内部状态选择处理函数，并生成本轮0x6041状态位。
 *
 * 状态与主站可见值的对应关系：
 *   Switch On Disabled -> 0x0040
 *   Ready To Switch On -> 0x0021
 *   Switched On        -> 0x0023
 *   Operation Enabled  -> 0x0027
 *   Quick Stop Active  -> 0x0007
 *   Fault              -> 0x0008
 */
static BOOL CiA402_ProcessState(TCiA402Axis *axis,
                                UINT16 control_word,
                                UINT16 *status_word)
{
    switch (axis->i16State)
    {
    case STATE_NOT_READY_TO_SWITCH_ON:
        *status_word |= STATUSWORD_STATE_NOTREADYTOSWITCHON;
        CiA402_HandleNotReady(axis);
        return TRUE;

    case STATE_SWITCH_ON_DISABLED:
        *status_word |= STATUSWORD_STATE_SWITCHEDONDISABLED;
        CiA402_HandleSwitchOnDisabled(axis, control_word);
        return TRUE;

    case STATE_READY_TO_SWITCH_ON:
        *status_word |= STATUSWORD_STATE_READYTOSWITCHON;
        CiA402_HandleReadyToSwitchOn(axis, control_word);
        return TRUE;

    case STATE_SWITCHED_ON:
        *status_word |= STATUSWORD_STATE_SWITCHEDON;
        CiA402_HandleSwitchedOn(axis, control_word);
        return TRUE;

    case STATE_OPERATION_ENABLED:
        *status_word |= STATUSWORD_STATE_OPERATIONENABLED;
        return CiA402_HandleOperationEnabled(axis,
                                             control_word);

    case STATE_QUICK_STOP_ACTIVE:
        *status_word |= STATUSWORD_STATE_QUICKSTOPACTIVE;
        return CiA402_HandleQuickStop(axis, control_word);

    case STATE_FAULT_REACTION_ACTIVE:
        *status_word |= STATUSWORD_STATE_FAULTREACTIONACTIVE;
        return CiA402_HandleFaultReaction(axis);

    case STATE_FAULT:
        *status_word |= STATUSWORD_STATE_FAULT;
        CiA402_HandleFault(axis, control_word);
        return TRUE;

    default:
        *status_word = STATUSWORD_STATE_NOTREADYTOSWITCHON;
        axis->i16State = STATE_NOT_READY_TO_SWITCH_ON;
        return TRUE;
    }
}

static void CiA402_SetDisabledFlags(TCiA402Axis *axis)
{
    /* 这些是软件状态标志，不会自动操作PWM或驱动使能引脚。 */
    axis->bBrakeApplied = TRUE;
    axis->bHighLevelPowerApplied = FALSE;
    axis->bAxisFunctionEnabled = FALSE;
    axis->bConfigurationAllowed = TRUE;
}

static void CiA402_UpdateAxisFlags(TCiA402Axis *axis)
{
    CiA402_SetDisabledFlags(axis);

    /* Switched On仅表示功率允许，电机算法还不能正常输出。 */
    if (axis->i16State == STATE_SWITCHED_ON)
    {
        axis->bHighLevelPowerApplied = TRUE;
        return;
    }

    if ((axis->i16State == STATE_OPERATION_ENABLED) ||
        (axis->i16State == STATE_QUICK_STOP_ACTIVE) ||
        (axis->i16State == STATE_FAULT_REACTION_ACTIVE))
    {
        /* Quick Stop和Fault Reaction仍可能需要算法执行受控停机。 */
        axis->bBrakeApplied = FALSE;
        axis->bHighLevelPowerApplied = TRUE;
        axis->bAxisFunctionEnabled = TRUE;
        axis->bConfigurationAllowed = FALSE;
    }
}

static void CiA402_WriteStatusWord(TCiA402Axis *axis,
                                   UINT16 status_word)
{
    if (axis->bHighLevelPowerApplied == TRUE)
    {
        status_word |= STATUSWORD_VOLTAGE_ENABLED;
    }
    else
    {
        status_word &= ~STATUSWORD_VOLTAGE_ENABLED;
    }

    /* Remote表示控制字已处理；Voltage Enabled由功率标志决定。 */
    axis->Objects.objStatusWord =
        status_word | STATUSWORD_REMOTE;
}

/**
 * @brief 周期处理0x6040控制字并更新0x6041状态字。
 *
 * 数据路径：
 *   主站RxPDO -> APPL_OutputMapping() -> objControlWord
 *   -> CiA402_StateMachine() -> i16State和objStatusWord
 *   -> APPL_InputMapping() -> 主站TxPDO
 *
 * 正常启动：0x0006 -> 0x0007 -> 0x000F。
 * 对应稳定状态：0x0021 -> 0x0023 -> 0x0027。
 *
 * 当前工程在EtherCAT OP时，会把低于Ready的内部状态直接提升到
 * Ready，因此0x0006可能表现为保持Ready，而不是再次切换。
 */
void CiA402_StateMachine(void)
{
    TCiA402Axis *axis;
    UINT16 status_word;
    UINT16 control_word;
    UINT16 axis_index;

    for (axis_index = 0U;
         axis_index < MAX_AXES;
         axis_index++)
    {
        axis = &LocalAxes[axis_index];
        if (axis->bAxisIsActive == FALSE)
        {
            continue;
        }

        if (axis->u16PendingOptionCode != 0U)
        {
            return;
        }

        /* OP快捷处理：跳过无真实硬件动作的前两个启动状态。 */
        if ((axis->i16State < STATE_READY_TO_SWITCH_ON) &&
            (nAlStatus == STATE_OP))
        {
            axis->i16State = STATE_READY_TO_SWITCH_ON;
        }

        status_word = axis->Objects.objStatusWord;
        status_word &= ~(STATUSWORD_STATE_MASK |
                         STATUSWORD_REMOTE);
        control_word = axis->Objects.objControlWord;

        /* 根据本轮进入函数时的状态解释控制字并切换内部状态。 */
        if (!CiA402_ProcessState(axis,
                                 control_word,
                                 &status_word))
        {
            return;
        }

        /* 使用切换后的状态更新软件使能标志，再写回0x6041。 */
        CiA402_UpdateAxisFlags(axis);
        CiA402_WriteStatusWord(axis, status_word);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param ErrorCode

 \brief    CiA402_LocalError
 \brief this function is called if an error was detected
*////////////////////////////////////////////////////////////////////////////////////////
void CiA402_LocalError(UINT16 ErrorCode)
{
    UINT16 counter = 0;

    /* 算法或硬件检测到故障时调用，先进入Fault Reaction Active。 */
    for(counter = 0; counter < MAX_AXES; counter++)
    {
        if(LocalAxes[counter].bAxisIsActive)
        {
            LocalAxes[counter].i16State = STATE_FAULT_REACTION_ACTIVE;
            LocalAxes[counter].Objects.objErrorCode = ErrorCode;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return TRUE if moving on predefined ramp is finished

 \brief    CiA402-TransitionAction
 \brief this function shall calculate the desired Axis input values to move on a predefined ramp
 \brief if the ramp is finished return TRUE
*////////////////////////////////////////////////////////////////////////////////////////
BOOL CiA402_TransitionAction(INT16 characteristic,
                            TCiA402Axis *axis)
{
    /*
     * 当前只是SSC示例停机动作：把速度反馈直接写0并立即报告完成。
     * 移植电机算法后，应在这里连接真实减速或故障停机完成条件。
     */
    switch (characteristic)
    {
    case SLOW_DOWN_RAMP:
    case QUICKSTOP_RAMP:
    case STOP_ON_CURRENT_LIMIT:
    case STOP_ON_VOLTAGE_LIMIT:
        axis->Objects.objVelocityActualValue = 0;
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

static INT16 CiA402_GetQuickStopRamp(INT16 option_code)
{
    if ((option_code >= 5) && (option_code <= 8))
    {
        return (INT16)(option_code - 4);
    }

    return option_code;
}

static void CiA402_RunQuickStop(TCiA402Axis *axis)
{
    INT16 option_code;
    INT16 ramp;

    option_code = axis->Objects.objQuickStopOptionCode;
    ramp = CiA402_GetQuickStopRamp(option_code);

    if (!CiA402_TransitionAction(ramp, axis))
    {
        return;
    }

    axis->u16PendingOptionCode = 0U;

    if ((option_code > 0) && (option_code < 5))
    {
        axis->i16State = STATE_SWITCH_ON_DISABLED;
        return;
    }

    if ((option_code >= 5) && (option_code <= 8))
    {
        axis->Objects.objStatusWord |=
            STATUSWORD_TARGET_REACHED;
    }
}

static void CiA402_RunPendingOption(TCiA402Axis *axis,
                                    INT16 option_code,
                                    INT16 next_state)
{
    if (!CiA402_TransitionAction(option_code, axis))
    {
        return;
    }

    axis->u16PendingOptionCode = 0U;
    axis->i16State = next_state;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    CiA402-Application
 \brief check if a state transition is pending and pass the desired
        ramp code to CiA402TransitionAction()
 \brief if this functions returns true the state transition is finished.
*////////////////////////////////////////////////////////////////////////////////////////
void CiA402_Application(TCiA402Axis *axis)
{
    /*
     * 这里只处理状态机挂起的0x605A、0x605B、0x605C和0x605E动作。
     * 正常0x0006、0x0007、0x000F转换在CiA402_StateMachine()中完成。
     */
    switch (axis->u16PendingOptionCode)
    {
    case 0x605AU:
        CiA402_RunQuickStop(axis);
        break;

    case 0x605BU:
        CiA402_RunPendingOption(
            axis,
            axis->Objects.objShutdownOptionCode,
            STATE_READY_TO_SWITCH_ON);
        break;

    case 0x605CU:
        CiA402_RunPendingOption(
            axis,
            axis->Objects.objDisableOperationOptionCode,
            STATE_SWITCHED_ON);
        break;

    case 0x605EU:
        CiA402_RunPendingOption(
            axis,
            axis->Objects.objFaultReactionCode,
            STATE_FAULT);
        break;

    default:
        axis->Objects.objStatusWord |=
            STATUSWORD_DRIVE_FOLLOWS_COMMAND;
        break;
    }
}


/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param     index                 index of the requested object.
 \param     subindex                subindex of the requested object.
 \param     dataSize                received data size of the SDO Download
 \param     pObjEntry            handle to the dictionary object returned by
                                     OBJ_GetObjectHandle which was called before
 \param    pData                    Pointer to the buffer where the written data can be copied from
 \param    bCompleteAccess    Indicates if a complete write of all subindices of the
                                     object shall be done or not

 \return    result of the write operation (0 (success) or an abort code (ABORTIDX_.... defined in
            sdosrv.h))

 \brief    This function writes "Configured Modules" Object 0xF030
*////////////////////////////////////////////////////////////////////////////////////////

UINT8 Write0xF030(UINT16 index,
                  UINT8 subindex,
                  UINT32 dataSize,
                  UINT16 MBXMEM *pData,
                  UINT8 bCompleteAccess)
{

    UINT16 i = subindex;
    UINT16 maxSubindex = sConfiguredModuleIdentList.u16SubIndex0;
    OBJCONST TSDOINFOENTRYDESC OBJMEM *pEntry;
    /* lastSubindex is used for complete access to make loop over the requested entries
       to be read, we initialize this variable with the requested subindex that only
       one loop will be done for a single access */
    UINT8 lastSubindex = subindex;

    if ( bCompleteAccess )
    {
        if ( subindex == 0 )
        {
            /* we change the subindex 0 */
            maxSubindex = (UINT8) pData[0];
        }

        /* we write until the maximum subindex */
        lastSubindex = (UINT8)maxSubindex;
    }
    else
        if (subindex > maxSubindex)
        {
            /* the maximum subindex is reached */
            return ABORTIDX_SUBINDEX_NOT_EXISTING;
        }
    else
    {
        /* we check the write access for single accesses here, a complete write access
           is allowed if at least one entry is writable (in this case the values for the
            read only entries shall be ignored) */
        /* we get the corresponding entry description */
        pEntry = &asEntryDesc0xF030[subindex];

        /* check if we have write access (bits 3-5 (PREOP, SAFEOP, OP) of ObjAccess)
           by comparing with the actual state (bits 1-3 (PREOP, SAFEOP, OP) of AL Status) */
        if (0 == (((UINT8)((pEntry->ObjAccess & ACCESS_WRITE) >> 2)) &
                  (nAlStatus & STATE_MASK)))
        {
            /* we don't have write access */
            if ( (pEntry->ObjAccess & ACCESS_WRITE) == 0 )
            {
                /* it is a read only entry */
                return ABORTIDX_READ_ONLY_ENTRY;
            }
            else
            {
                /* we don't have write access in this state */
                return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
            }
        }
    }

        /* we use the standard write function */
        for (i = subindex; i <= lastSubindex; i++)
        {
            /* we have to copy the entry */
            if (i == 0)
            {
                /*check if the value for subindex0 is valid */
                if(MAX_AXES < (UINT8) pData[0])
                {
                    return ABORTIDX_VALUE_TOO_GREAT;
                }

                sConfiguredModuleIdentList.u16SubIndex0 =  pData[0];

                /* we increment the destination pointer by 2 because the subindex 0 will be
                    transmitted as UINT16 for a complete access */
                pData++;
            }
            else
            {
                UINT32 CurValue = sConfiguredModuleIdentList.aEntries[(i-1)];
                UINT16 MBXMEM *pVarPtr;

                pVarPtr = (UINT16 MBXMEM *)
                    &sConfiguredModuleIdentList.aEntries[i - 1U];

                pVarPtr[0] = pData[0];
                pVarPtr[1] = pData[1];
                pData += 2;

                /*Check if valid value was written*/
                if((sConfiguredModuleIdentList.aEntries[(i-1)] != (UINT32)CSV_CSP_MODULE_ID)
                && (sConfiguredModuleIdentList.aEntries[(i-1)] != (UINT32)CSP_MODULE_ID)
                && (sConfiguredModuleIdentList.aEntries[(i-1)] != (UINT32)CSV_MODULE_ID)
                && (sConfiguredModuleIdentList.aEntries[(i-1)] != 0))
                {
                    /*write previous value*/
                    sConfiguredModuleIdentList.aEntries[(i-1)] = CurValue;

                    /*reset subindex 0 (if required)*/
                    if(sConfiguredModuleIdentList.aEntries[(i-1)] != 0)
                    {
                        sConfiguredModuleIdentList.u16SubIndex0 = i;
                    }
                    else
                    {
                        /*current entry is 0 => set subindex0 value i-1*/
                        sConfiguredModuleIdentList.u16SubIndex0 = (i-1);
                    }


                    return ABORTIDX_VALUE_EXCEEDED;
                }
           }
        }

        /*Update PDO assign objects and 0xF010 (Module Profile List)*/
        {
        UINT8 cnt = 0;

        /*Update 0xF010.0 */
        sModuleProfileInfo.u16SubIndex0 = sConfiguredModuleIdentList.u16SubIndex0;
        
        /*Update PDO assign SI0*/
        sRxPDOassign.u16SubIndex0 = sConfiguredModuleIdentList.u16SubIndex0;
        sTxPDOassign.u16SubIndex0 = sConfiguredModuleIdentList.u16SubIndex0;

        for (cnt = 0 ; cnt < sConfiguredModuleIdentList.u16SubIndex0; cnt++)
        {
            /*all Modules have the same profile number*/
/*ECATCHANGE_START(V5.13) CIA402 2*/
            sModuleProfileInfo.aEntries[cnt] = (DEVICE_PROFILE_TYPE >> 16);
/*ECATCHANGE_END(V5.13) CIA402 2*/

            switch(sConfiguredModuleIdentList.aEntries[cnt])
            {
                case CSV_CSP_MODULE_ID:
                    sRxPDOassign.aEntries[cnt] = (0x1600 +(0x10*cnt));
                    sTxPDOassign.aEntries[cnt] = (0x1A00 +(0x10*cnt));
                break;
                case CSP_MODULE_ID:
                    sRxPDOassign.aEntries[cnt] = (0x1601 +(0x10*cnt));
                    sTxPDOassign.aEntries[cnt] = (0x1A01 +(0x10*cnt));
                break;
                case CSV_MODULE_ID:
                    sRxPDOassign.aEntries[cnt] = (0x1602 +(0x10*cnt));
                    sTxPDOassign.aEntries[cnt] = (0x1A02 +(0x10*cnt));
                break;
                default:
                    sRxPDOassign.aEntries[cnt] = 0;
                    sTxPDOassign.aEntries[cnt] = 0;

                    sModuleProfileInfo.aEntries[cnt] = 0;
                break;
            }
        }
        }

    return 0;
}

/*-----------------------------------------------------------------------------------------
------
------    generic functions
------
-----------------------------------------------------------------------------------------*/
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    The function is called when an error state was acknowledged by the master

*////////////////////////////////////////////////////////////////////////////////////////

void    APPL_AckErrorInd(UINT16 stateTrans)
{
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from INIT to PREOP when
           all general settings were checked to start the mailbox handler. This function
           informs the application about the state transition, the application can refuse
           the state transition when returning an AL Status error code.
           The return code NOERROR_INWORK can be used, if the application cannot confirm
           the state transition immediately, in that case the application need to be complete 
           the transition by calling ECAT_StateChange.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartMailboxHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from PREEOP to INIT
           to stop the mailbox handler. This functions informs the application
           about the state transition, the application cannot refuse
           the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopMailboxHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param    pIntMask    pointer to the AL Event Mask which will be written to the AL event Mask
                        register (0x204) when this function is succeeded.
                        The event mask can be adapted
                        in this function
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from PREOP to SAFEOP when
             all general settings were checked to start the input handler. This function
             informs the application about the state transition, the application can refuse
             the state transition when returning an AL Status error code.
            The return code NOERROR_INWORK can be used, if the application cannot confirm
            the state transition immediately, in that case the application need to be complete 
            the transition by calling ECAT_StateChange.
*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartInputHandler(UINT16 *pIntMask)
{
    UINT32 Sync0CycleTime = 0;
    if(sConfiguredModuleIdentList.u16SubIndex0 == 0)
    {
        /* Object 0xF030 was not written before 
        * => update object 0xF010 (Module profile list) and 0xF050 (Detected Module List)*/
    
        UINT8 cnt = 0;


        /*Update 0xF010.0 */
        sModuleProfileInfo.u16SubIndex0 = sRxPDOassign.u16SubIndex0;

        /*Update 0xF050.0*/
        sDetectedModuleIdentList.u16SubIndex0 = sRxPDOassign.u16SubIndex0;
        
        for (cnt = 0 ; cnt < sRxPDOassign.u16SubIndex0; cnt++)
        {
            /*all Modules have the same profile number*/
/*ECATCHANGE_START(V5.13) CIA402 2*/
            sModuleProfileInfo.aEntries[cnt] = (DEVICE_PROFILE_TYPE >> 16);
/*ECATCHANGE_END(V5.13) CIA402 2*/

            switch (sRxPDOassign.aEntries[cnt] & 0x000FU)
            {
                case 0x0:   //csv/csp PDO selected
                    sDetectedModuleIdentList.aEntries[cnt] = CSV_CSP_MODULE_ID;
                break;
                case 0x1:   //csp PDO selected
                    sDetectedModuleIdentList.aEntries[cnt] = CSP_MODULE_ID;
                break;
                case 0x2:   //csv PDO selected
                    sDetectedModuleIdentList.aEntries[cnt] = CSV_MODULE_ID;
                break;

            }
            
        }
    }

    HW_EscReadDWord(Sync0CycleTime, ESC_DC_SYNC0_CYCLETIME_OFFSET);
    Sync0CycleTime = SWAPDWORD(Sync0CycleTime);

    /*Init CiA402 structure if the device is in SM Sync mode
    the CiA402 structure will be Initialized after calculation of the Cycle time*/
    if(bDcSyncActive == TRUE)
    {
        UINT16 i;
        Sync0CycleTime = Sync0CycleTime / 1000; //get cycle time in us
        for(i = 0; i< MAX_AXES;i++)
        {
                if (LocalAxes[i].bAxisIsActive)
                {
                    LocalAxes[i].u32CycleTime = Sync0CycleTime;
                }
        }
    }

    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from SAFEOP to PREEOP
             to stop the input handler. This functions informs the application
             about the state transition, the application cannot refuse
             the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopInputHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from SAFEOP to OP when
             all general settings were checked to start the output handler. This function
             informs the application about the state transition, the application can refuse
             the state transition when returning an AL Status error code.
           The return code NOERROR_INWORK can be used, if the application cannot confirm
           the state transition immediately, in that case the application need to be complete 
           the transition by calling ECAT_StateChange.
*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartOutputHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from OP to SAFEOP
             to stop the output handler. This functions informs the application
             about the state transition, the application cannot refuse
             the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopOutputHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\return     0(ALSTATUSCODE_NOERROR), NOERROR_INWORK
\param      pInputSize  pointer to save the input process data length
\param      pOutputSize  pointer to save the output process data length

\brief    This function calculates the process data sizes from the actual SM-PDO-Assign
            and PDO mapping
*////////////////////////////////////////////////////////////////////////////////////////
UINT16 APPL_GenerateMapping(UINT16* pInputSize,UINT16* pOutputSize)
{
    UINT16 result = ALSTATUSCODE_NOERROR;
    UINT16 PDOAssignEntryCnt = 0;
    UINT8 AxisIndex = 0;
    UINT16 InputSize = 0;
    UINT16 OutputSize = 0;
    TOBJECT OBJMEM *pDiCEntry = NULL;


    if (sRxPDOassign.u16SubIndex0 > MAX_AXES)
    {
        return ALSTATUSCODE_NOVALIDOUTPUTS;
    }
    if (sTxPDOassign.u16SubIndex0 > MAX_AXES)
    {
        return ALSTATUSCODE_NOVALIDINPUTS;
    }

    /*Update object dictionary according to activated axis
    which axes are activated is calculated by object 0x1C12*/
    for (PDOAssignEntryCnt = 0U;
         PDOAssignEntryCnt < sRxPDOassign.u16SubIndex0;
         PDOAssignEntryCnt++)
    {
        /*The PDO mapping objects are specified with an 0x10 offset => get the axis index*/
        AxisIndex = (sRxPDOassign.aEntries[PDOAssignEntryCnt] & 0xF0) >> 4;

        if(AxisIndex == PDOAssignEntryCnt)
        {
            /* Add axis objects when the axis enters process data. */

            if(!LocalAxes[PDOAssignEntryCnt].bAxisIsActive)
            {
                /*add objects to dictionary*/
                pDiCEntry = LocalAxes[PDOAssignEntryCnt].ObjDic;

                while(pDiCEntry->Index != 0xFFFF)
                {
                    result = COE_AddObjectToDic(pDiCEntry);

                    if(result != 0)
                    {
                        return result;
                    }

                    pDiCEntry++;    //get next entry
                }

                
                LocalAxes[PDOAssignEntryCnt].bAxisIsActive = TRUE;
            }

        }
        else
        {
            /* Remove axis objects when the axis leaves process data. */
            if(LocalAxes[PDOAssignEntryCnt].bAxisIsActive)
            {
                /*add objects to dictionary*/
                pDiCEntry = LocalAxes[PDOAssignEntryCnt].ObjDic;

                while(pDiCEntry->Index != 0xFFFF)
                {
                    COE_RemoveDicEntry(pDiCEntry->Index);

                    pDiCEntry++;    //get next entry
                }

                
                LocalAxes[PDOAssignEntryCnt].bAxisIsActive = FALSE;
            }
        }

    }

    /*Scan object 0x1C12 RXPDO assign*/
    for(PDOAssignEntryCnt = 0; PDOAssignEntryCnt < sRxPDOassign.u16SubIndex0; PDOAssignEntryCnt++)
    {
        UINT16 mappingSizeBits = 0U;

        switch (sRxPDOassign.aEntries[PDOAssignEntryCnt] &
                0x000FU)
        {
            case 0:
                /* PP: bit 0, PV: bit 2, HM: bit 5, CSP: bit 7, CSV: bit 8. */
                LocalAxes[PDOAssignEntryCnt].Objects.objSupportedDriveModes = 0x1A5;
                break;
            case 1:
                /* Position-oriented PDO: PP, HM and CSP. */
                LocalAxes[PDOAssignEntryCnt].Objects.objSupportedDriveModes = 0xA1;
                break;
            case 2:
                /* Velocity-oriented PDO: PV, HM and CSV. */
                LocalAxes[PDOAssignEntryCnt].Objects.objSupportedDriveModes= 0x124;
                break;
            default:
                return ALSTATUSCODE_INVALIDOUTPUTMAPPING;
        }

        result = CiA402_GetPdoSizeBits(sRxPDOassign.aEntries[PDOAssignEntryCnt],
                                       FALSE, &mappingSizeBits);
        if (result != ALSTATUSCODE_NOERROR)
        {
            return result;
        }
        OutputSize = (UINT16)(OutputSize + mappingSizeBits);
    }

    OutputSize = OutputSize >> 3;

    if(result == 0)
    {
        /*Scan Object 0x1C13 TXPDO assign*/
        for (PDOAssignEntryCnt = 0U;
             PDOAssignEntryCnt < sTxPDOassign.u16SubIndex0;
             PDOAssignEntryCnt++)
        {
            UINT16 mappingSizeBits = 0U;

            result = CiA402_GetPdoSizeBits(sTxPDOassign.aEntries[PDOAssignEntryCnt],
                                           TRUE, &mappingSizeBits);
            if (result != ALSTATUSCODE_NOERROR)
            {
                return result;
            }
            InputSize = (UINT16)(InputSize + mappingSizeBits);
        }
        
        InputSize = InputSize >> 3;
    }

    *pInputSize = InputSize;
    *pOutputSize = OutputSize;
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\param      pData  pointer to input process data
\brief      This function will copies the inputs from the local memory to the ESC memory
            to the hardware
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_InputMapping(UINT16* pData)
{
    UINT16 j = 0;
    UINT16 processOffsetBytes = 0U;
    UINT8 *pTmpData = (UINT8 *)pData;

    /*
     * TxPDO方向：MCU -> ESC -> 主站。
     * 这里发送0x6041状态字、实际位置、实际速度和实际模式。
     */
    for (j = 0; j < sTxPDOassign.u16SubIndex0; j++)
    {
        CiA402_CopyPdo(sTxPDOassign.aEntries[j], pTmpData, TRUE,
                       &processOffsetBytes);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\param      pData  pointer to output process data

\brief    This function will copies the outputs from the ESC memory to the local memory
            to the hardware
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_OutputMapping(UINT16* pData)
{
    UINT16 j = 0;
    UINT16 processOffsetBytes = 0U;
    UINT8 *pTmpData = (UINT8 *)pData;

    /*
     * RxPDO方向：主站 -> ESC -> MCU。
     * 这里接收0x6040控制字、目标位置、目标速度和控制模式。
     * 接收后由CiA402_StateMachine()解释控制字0x0006、0x0007、0x000F。
     */
    for (j = 0; j < sRxPDOassign.u16SubIndex0; j++)
    {
        CiA402_CopyPdo(sRxPDOassign.aEntries[j], pTmpData, FALSE,
                       &processOffsetBytes);
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
/**
\brief    This function will called from the synchronisation ISR 
            or from the mainloop if no synchronisation is supported
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_Application(void)
{
    UINT16 axis_index;

    for (axis_index = 0U;
         axis_index < MAX_AXES;
         axis_index++)
    {
        if (LocalAxes[axis_index].bAxisIsActive == FALSE)
        {
            continue;
        }

        /*
         * 当前只处理待完成的停止/故障动作，没有启动PWM。
         * 电机算法应在Operation Enabled时由此处使能。
         */
        CiA402_Application(&LocalAxes[axis_index]);
    }
}

/** @} */

/** @}
void APPL_Application(void)
{
    TCiA402Axis *axis;

    axis = &LocalAxes[0];
    CiA402_Application(axis);

    if (((nAlStatus & STATE_MASK) == STATE_OP) &&
        (axis->i16State == STATE_OPERATION_ENABLED))
    {
        motor_control_enable();

        motor_control_set_command(
            axis->Objects.objModesOfOperation,
            axis->Objects.objTargetPosition,
            axis->Objects.objTargetVelocity);
    }
    else
    {
        motor_control_disable();
    }

    axis->Objects.objPositionActualValue =
        motor_control_get_position();

    axis->Objects.objVelocityActualValue =
        motor_control_get_velocity();

    axis->Objects.objModesOfOperationDisplay =
        axis->Objects.objModesOfOperation;
}
*/

