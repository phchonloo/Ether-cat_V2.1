/*
* This source file is part of the EtherCAT Slave Stack Code licensed by Beckhoff Automation GmbH & Co KG, 33415 Verl, Germany.
* The corresponding license agreement applies. This hint shall not be removed.
*/

/**
* \addtogroup el9800appl el9800appl
* @{
*/

/**
\file el9800applObjects
\author ET9300Utilities.ApplicationHandler (Version 1.6.4.0) | EthercatSSC@beckhoff.com

\brief el9800appl specific objects<br>
\brief NOTE : This file will be overwritten if a new object dictionary is generated!<br>
*/

#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
#define PROTO
#else
#define PROTO extern
#endif
/******************************************************************************
*                    Object 0x1600 : DO RxPDO-Map
******************************************************************************/
/**
* \addtogroup 0x1600 0x1600 | DO RxPDO-Map
* @{
* \brief Object 0x1600 (DO RxPDO-Map) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1<br>
* SubIndex 2<br>
* SubIndex 3<br>
* SubIndex 4<br>
* SubIndex 5<br>
* SubIndex 6<br>
* SubIndex 7<br>
* SubIndex 8<br>
* SubIndex 9<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1600[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex1 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex2 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex3 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex4 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex5 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex6 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex7 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex8 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }}; /* Subindex9 */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x1600[] = "DO RxPDO-Map\000"
"SubIndex 001\000"
"SubIndex 002\000"
"SubIndex 003\000"
"SubIndex 004\000"
"SubIndex 005\000"
"SubIndex 006\000"
"SubIndex 007\000"
"SubIndex 008\000"
"SubIndex 009\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
UINT32 SI1; /* Subindex1 -  */
UINT32 SI2; /* Subindex2 -  */
UINT32 SI3; /* Subindex3 -  */
UINT32 SI4; /* Subindex4 -  */
UINT32 SI5; /* Subindex5 -  */
UINT32 SI6; /* Subindex6 -  */
UINT32 SI7; /* Subindex7 -  */
UINT32 SI8; /* Subindex8 -  */
UINT32 SI9; /* Subindex9 -  */
} OBJ_STRUCT_PACKED_END
TOBJ1600;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1600 DORxPDOMap0x1600
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={9,0x70000101,0x70000201,0x70000301,0x70000401,0x70000501,0x70000601,0x70000701,0x70000801,0x00000008}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x1802 : TxPDOParameter
******************************************************************************/
/**
* \addtogroup 0x1802 0x1802 | TxPDOParameter
* @{
* \brief Object 0x1802 (TxPDOParameter) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1 does not exists<br>
* SubIndex 2 does not exists<br>
* SubIndex 3 does not exists<br>
* SubIndex 4 does not exists<br>
* SubIndex 5 does not exists<br>
* SubIndex 6 does not exists<br>
* SubIndex 7 - TxPDOState<br>
* SubIndex 8 does not exists<br>
* SubIndex 9 - TxPDOToggle<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1802[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex1 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex2 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex3 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex4 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex5 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex6 does not exists */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex7 - TxPDOState */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex8 does not exists */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }}; /* Subindex9 - TxPDOToggle */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x1802[] = "TxPDOParameter\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"TxPDOState\000"
"\000"
"TxPDOToggle\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
BOOLEAN(TxPDOState); /* Subindex7 - TxPDOState */
BOOLEAN(TxPDOToggle); /* Subindex9 - TxPDOToggle */
} OBJ_STRUCT_PACKED_END
TOBJ1802;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1802 TxPDOParameter0x1802
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={9,0,0}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x1A00 : DI TxPDO-Map
******************************************************************************/
/**
* \addtogroup 0x1A00 0x1A00 | DI TxPDO-Map
* @{
* \brief Object 0x1A00 (DI TxPDO-Map) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1<br>
* SubIndex 2<br>
* SubIndex 3<br>
* SubIndex 4<br>
* SubIndex 5<br>
* SubIndex 6<br>
* SubIndex 7<br>
* SubIndex 8<br>
* SubIndex 9<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1A00[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex1 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex2 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex3 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex4 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex5 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex6 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex7 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex8 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }}; /* Subindex9 */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x1A00[] = "DI TxPDO-Map\000"
"SubIndex 001\000"
"SubIndex 002\000"
"SubIndex 003\000"
"SubIndex 004\000"
"SubIndex 005\000"
"SubIndex 006\000"
"SubIndex 007\000"
"SubIndex 008\000"
"SubIndex 009\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
UINT32 SI1; /* Subindex1 -  */
UINT32 SI2; /* Subindex2 -  */
UINT32 SI3; /* Subindex3 -  */
UINT32 SI4; /* Subindex4 -  */
UINT32 SI5; /* Subindex5 -  */
UINT32 SI6; /* Subindex6 -  */
UINT32 SI7; /* Subindex7 -  */
UINT32 SI8; /* Subindex8 -  */
UINT32 SI9; /* Subindex9 -  */
} OBJ_STRUCT_PACKED_END
TOBJ1A00;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1A00 DITxPDOMap0x1A00
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={9,0x60000101,0x60000201,0x60000301,0x60000401,0x60000501,0x60000601,0x60000701,0x60000801,0x00000008}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x1A02 : AI TxPDO-Map
******************************************************************************/
/**
* \addtogroup 0x1A02 0x1A02 | AI TxPDO-Map
* @{
* \brief Object 0x1A02 (AI TxPDO-Map) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1<br>
* SubIndex 2<br>
* SubIndex 3<br>
* SubIndex 4<br>
* SubIndex 5<br>
* SubIndex 6<br>
* SubIndex 7<br>
* SubIndex 8<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1A02[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex1 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex2 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex3 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex4 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex5 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex6 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }, /* Subindex7 */
{ DEFTYPE_UNSIGNED32 , 0x20 , ACCESS_READ }}; /* Subindex8 */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x1A02[] = "AI TxPDO-Map\000"
"SubIndex 001\000"
"SubIndex 002\000"
"SubIndex 003\000"
"SubIndex 004\000"
"SubIndex 005\000"
"SubIndex 006\000"
"SubIndex 007\000"
"SubIndex 008\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
UINT32 SI1; /* Subindex1 -  */
UINT32 SI2; /* Subindex2 -  */
UINT32 SI3; /* Subindex3 -  */
UINT32 SI4; /* Subindex4 -  */
UINT32 SI5; /* Subindex5 -  */
UINT32 SI6; /* Subindex6 -  */
UINT32 SI7; /* Subindex7 -  */
UINT32 SI8; /* Subindex8 -  */
} OBJ_STRUCT_PACKED_END
TOBJ1A02;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1A02 AITxPDOMap0x1A02
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={8,0x60200101,0x60200201,0x60200302,0x60200502,0x00000008,0x18020701,0x18020901,0x60201110}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x1C12 : RxPDO assign
******************************************************************************/
/**
* \addtogroup 0x1C12 0x1C12 | RxPDO assign
* @{
* \brief Object 0x1C12 (RxPDO assign) definition
*/
#ifdef _OBJD_
/**
* \brief Entry descriptions<br>
* 
* Subindex 0<br>
* Subindex 1 - n (the same entry description is used)<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1C12[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ | ACCESS_WRITE_PREOP },
{ DEFTYPE_UNSIGNED16 , 0x10 , ACCESS_READ | ACCESS_WRITE_PREOP }};

/**
* \brief Object name definition<br>
* For Subindex 1 to n the syntax 'Subindex XXX' is used
*/
OBJCONST UCHAR OBJMEM aName0x1C12[] = "RxPDO assign\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16   u16SubIndex0;  /**< \brief Subindex 0 */
UINT16 aEntries[1];  /**< \brief Subindex 1 - 1 */
} OBJ_STRUCT_PACKED_END
TOBJ1C12;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1C12 sRxPDOassign
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={1,{0x1600}}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x1C13 : TxPDO assign
******************************************************************************/
/**
* \addtogroup 0x1C13 0x1C13 | TxPDO assign
* @{
* \brief Object 0x1C13 (TxPDO assign) definition
*/
#ifdef _OBJD_
/**
* \brief Entry descriptions<br>
* 
* Subindex 0<br>
* Subindex 1 - n (the same entry description is used)<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x1C13[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ | ACCESS_WRITE_PREOP },
{ DEFTYPE_UNSIGNED16 , 0x10 , ACCESS_READ | ACCESS_WRITE_PREOP }};

/**
* \brief Object name definition<br>
* For Subindex 1 to n the syntax 'Subindex XXX' is used
*/
OBJCONST UCHAR OBJMEM aName0x1C13[] = "TxPDO assign\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16   u16SubIndex0;  /**< \brief Subindex 0 */
UINT16 aEntries[2];  /**< \brief Subindex 1 - 2 */
} OBJ_STRUCT_PACKED_END
TOBJ1C13;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ1C13 sTxPDOassign
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={2,{0x1A00,0x1A02}}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x6000 : DI Inputs
******************************************************************************/
/**
* \addtogroup 0x6000 0x6000 | DI Inputs
* @{
* \brief Object 0x6000 (DI Inputs) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1 - Switch1<br>
* SubIndex 2 - Switch2<br>
* SubIndex 3 - Switch3<br>
* SubIndex 4 - Switch4<br>
* SubIndex 5 - Switch5<br>
* SubIndex 6 - Switch6<br>
* SubIndex 7 - Switch7<br>
* SubIndex 8 - Switch8<br>
* SubIndex 9<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x6000[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex1 - Switch1 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex2 - Switch2 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex3 - Switch3 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex4 - Switch4 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex5 - Switch5 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex6 - Switch6 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex7 - Switch7 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex8 - Switch8 */
{ DEFTYPE_NULL , 0x08 , 0x0000 }}; /* Subindex9 */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x6000[] = "DI Inputs\000"
"Switch1\000"
"Switch2\000"
"Switch3\000"
"Switch4\000"
"Switch5\000"
"Switch6\000"
"Switch7\000"
"Switch8\000"
"\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
BOOLEAN(Switch1); /* Subindex1 - Switch1 */
BOOLEAN(Switch2); /* Subindex2 - Switch2 */
BOOLEAN(Switch3); /* Subindex3 - Switch3 */
BOOLEAN(Switch4); /* Subindex4 - Switch4 */
BOOLEAN(Switch5); /* Subindex5 - Switch5 */
BOOLEAN(Switch6); /* Subindex6 - Switch6 */
BOOLEAN(Switch7); /* Subindex7 - Switch7 */
BOOLEAN(Switch8); /* Subindex8 - Switch8 */
ALIGN8(SI9) /* Subindex9 */
} OBJ_STRUCT_PACKED_END
TOBJ6000;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ6000 DIInputs0x6000
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={8,0,0,0,0,0,0,0,0,0}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x6020 : AI Inputs
******************************************************************************/
/**
* \addtogroup 0x6020 0x6020 | AI Inputs
* @{
* \brief Object 0x6020 (AI Inputs) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1 - Underrange<br>
* SubIndex 2 - Overrange<br>
* SubIndex 3 - Limit 1<br>
* SubIndex 4 does not exists<br>
* SubIndex 5 - Limit 2<br>
* SubIndex 6<br>
* SubIndex 7<br>
* SubIndex 8 does not exists<br>
* SubIndex 9 does not exists<br>
* SubIndex 10 does not exists<br>
* SubIndex 11 does not exists<br>
* SubIndex 12 does not exists<br>
* SubIndex 13 does not exists<br>
* SubIndex 14 does not exists<br>
* SubIndex 15 - TxPDO State<br>
* SubIndex 16 - TxPDO Toggle<br>
* SubIndex 17 - Analoginput<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x6020[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex1 - Underrange */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex2 - Overrange */
{ DEFTYPE_BIT2 , 0x02 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex3 - Limit 1 */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex4 does not exists */
{ DEFTYPE_BIT2 , 0x02 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex5 - Limit 2 */
{ DEFTYPE_NULL , 0x02 , 0x0000 }, /* Subindex6 */
{ DEFTYPE_NULL , 0x06 , 0x0000 }, /* Subindex7 */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex8 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex9 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex10 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex11 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex12 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex13 does not exists */
{ DEFTYPE_NULL , 0x00 , 0x0000 }, /* Subindex14 does not exists */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex15 - TxPDO State */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }, /* Subindex16 - TxPDO Toggle */
{ DEFTYPE_UNSIGNED16 , 0x10 , ACCESS_READ | OBJACCESS_TXPDOMAPPING }}; /* Subindex17 - Analoginput */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x6020[] = "AI Inputs\000"
"Underrange\000"
"Overrange\000"
"Limit 1\000"
"\000"
"Limit 2\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"\000"
"TxPDO State\000"
"TxPDO Toggle\000"
"Analoginput\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
BOOLEAN(Underrange); /* Subindex1 - Underrange */
BOOLEAN(Overrange); /* Subindex2 - Overrange */
BIT2(Limit1); /* Subindex3 - Limit 1 */
BIT2(Limit2); /* Subindex5 - Limit 2 */
ALIGN2(SI6) /* Subindex6 */
ALIGN6(SI7) /* Subindex7 */
BOOLEAN(TxPDOState); /* Subindex15 - TxPDO State */
BOOLEAN(TxPDOToggle); /* Subindex16 - TxPDO Toggle */
UINT16 Analoginput; /* Subindex17 - Analoginput */
} OBJ_STRUCT_PACKED_END
TOBJ6020;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ6020 AIInputs0x6020
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={17,0,0,0,0,0,0,0,0,0}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0x7000 : DO Outputs
******************************************************************************/
/**
* \addtogroup 0x7000 0x7000 | DO Outputs
* @{
* \brief Object 0x7000 (DO Outputs) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1 - LED1<br>
* SubIndex 2 - LED2<br>
* SubIndex 3 - LED3<br>
* SubIndex 4 - LED4<br>
* SubIndex 5 - LED5<br>
* SubIndex 6 - LED6<br>
* SubIndex 7 - LED7<br>
* SubIndex 8 - LED8<br>
* SubIndex 9<br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0x7000[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex1 - LED1 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex2 - LED2 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex3 - LED3 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex4 - LED4 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex5 - LED5 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex6 - LED6 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex7 - LED7 */
{ DEFTYPE_BOOLEAN , 0x01 , ACCESS_READ | OBJACCESS_RXPDOMAPPING }, /* Subindex8 - LED8 */
{ DEFTYPE_NULL , 0x08 , 0x0000 }}; /* Subindex9 */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0x7000[] = "DO Outputs\000"
"LED1\000"
"LED2\000"
"LED3\000"
"LED4\000"
"LED5\000"
"LED6\000"
"LED7\000"
"LED8\000"
"\000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
BOOLEAN(LED1); /* Subindex1 - LED1 */
BOOLEAN(LED2); /* Subindex2 - LED2 */
BOOLEAN(LED3); /* Subindex3 - LED3 */
BOOLEAN(LED4); /* Subindex4 - LED4 */
BOOLEAN(LED5); /* Subindex5 - LED5 */
BOOLEAN(LED6); /* Subindex6 - LED6 */
BOOLEAN(LED7); /* Subindex7 - LED7 */
BOOLEAN(LED8); /* Subindex8 - LED8 */
ALIGN8(SI9) /* Subindex9 */
} OBJ_STRUCT_PACKED_END
TOBJ7000;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJ7000 DOOutputs0x7000
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={8,0,0,0,0,0,0,0,0,0}
#endif
;
/** @}*/



/******************************************************************************
*                    Object 0xF000 : Modular Device Profile
******************************************************************************/
/**
* \addtogroup 0xF000 0xF000 | Modular Device Profile
* @{
* \brief Object 0xF000 (Modular Device Profile) definition
*/
#ifdef _OBJD_
/**
* \brief Object entry descriptions<br>
* <br>
* SubIndex 0<br>
* SubIndex 1 - Index distance <br>
* SubIndex 2 - Maximum number of modules <br>
*/
OBJCONST TSDOINFOENTRYDESC    OBJMEM asEntryDesc0xF000[] = {
{ DEFTYPE_UNSIGNED8 , 0x8 , ACCESS_READ },
{ DEFTYPE_UNSIGNED16 , 0x10 , ACCESS_READ }, /* Subindex1 - Index distance  */
{ DEFTYPE_UNSIGNED16 , 0x10 , ACCESS_READ }}; /* Subindex2 - Maximum number of modules  */

/**
* \brief Object/Entry names
*/
OBJCONST UCHAR OBJMEM aName0xF000[] = "Modular Device Profile\000"
"Index distance \000"
"Maximum number of modules \000\377";
#endif //#ifdef _OBJD_

#ifndef _EL9800APPL_OBJECTS_H_
/**
* \brief Object structure
*/
typedef struct OBJ_STRUCT_PACKED_START {
UINT16 u16SubIndex0;
UINT16 IndexDistance; /* Subindex1 - Index distance  */
UINT16 MaximumNumberOfModules; /* Subindex2 - Maximum number of modules  */
} OBJ_STRUCT_PACKED_END
TOBJF000;
#endif //#ifndef _EL9800APPL_OBJECTS_H_

/**
* \brief Object variable
*/
PROTO TOBJF000 ModularDeviceProfile0xF000
#if defined(_EL9800APPL_) && (_EL9800APPL_ == 1)
={2,0x0010,0}
#endif
;
/** @}*/





PROTO UINT8 ReadObject0x1802(UINT16 index, UINT8 subindex, UINT32 dataSize, UINT16 MBXMEM * pData, UINT8 bCompleteAccess);


#ifdef _OBJD_
TOBJECT    OBJMEM ApplicationObjDic[] = {
/* Object 0x1600 */
{NULL , NULL ,  0x1600 , {DEFTYPE_PDOMAPPING , 9 | (OBJCODE_REC << 8)} , asEntryDesc0x1600 , aName0x1600 , &DORxPDOMap0x1600 , NULL , NULL , 0x0000 },
/* Object 0x1802 */
{NULL , NULL ,  0x1802 , {DEFTYPE_RECORD , 9 | (OBJCODE_REC << 8)} , asEntryDesc0x1802 , aName0x1802 , &TxPDOParameter0x1802 , ReadObject0x1802 , NULL , 0x0000 },
/* Object 0x1A00 */
{NULL , NULL ,  0x1A00 , {DEFTYPE_PDOMAPPING , 9 | (OBJCODE_REC << 8)} , asEntryDesc0x1A00 , aName0x1A00 , &DITxPDOMap0x1A00 , NULL , NULL , 0x0000 },
/* Object 0x1A02 */
{NULL , NULL ,  0x1A02 , {DEFTYPE_PDOMAPPING , 8 | (OBJCODE_REC << 8)} , asEntryDesc0x1A02 , aName0x1A02 , &AITxPDOMap0x1A02 , NULL , NULL , 0x0000 },
/* Object 0x1C12 */
{NULL , NULL ,  0x1C12 , {DEFTYPE_UNSIGNED16 , 1 | (OBJCODE_ARR << 8)} , asEntryDesc0x1C12 , aName0x1C12 , &sRxPDOassign , NULL , NULL , 0x0000 },
/* Object 0x1C13 */
{NULL , NULL ,  0x1C13 , {DEFTYPE_UNSIGNED16 , 2 | (OBJCODE_ARR << 8)} , asEntryDesc0x1C13 , aName0x1C13 , &sTxPDOassign , NULL , NULL , 0x0000 },
/* Object 0x6000 */
{NULL , NULL ,  0x6000 , {DEFTYPE_RECORD , 9 | (OBJCODE_REC << 8)} , asEntryDesc0x6000 , aName0x6000 , &DIInputs0x6000 , NULL , NULL , 0x0000 },
/* Object 0x6020 */
{NULL , NULL ,  0x6020 , {DEFTYPE_RECORD , 17 | (OBJCODE_REC << 8)} , asEntryDesc0x6020 , aName0x6020 , &AIInputs0x6020 , NULL , NULL , 0x0000 },
/* Object 0x7000 */
{NULL , NULL ,  0x7000 , {DEFTYPE_RECORD , 9 | (OBJCODE_REC << 8)} , asEntryDesc0x7000 , aName0x7000 , &DOOutputs0x7000 , NULL , NULL , 0x0000 },
/* Object 0xF000 */
{NULL , NULL ,  0xF000 , {DEFTYPE_RECORD , 2 | (OBJCODE_REC << 8)} , asEntryDesc0xF000 , aName0xF000 , &ModularDeviceProfile0xF000 , NULL , NULL , 0x0000 },
{NULL,NULL, 0xFFFF, {0, 0}, NULL, NULL, NULL, NULL}};
#endif    //#ifdef _OBJD_

#undef PROTO

/** @}*/
#define _EL9800APPL_OBJECTS_H_
