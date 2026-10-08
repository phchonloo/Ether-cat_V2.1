#ifndef _ERRORHADLE_H_
#define _ERRORHADLE_H_

#include "mcuinit.h"


void ErrorHandle(void);
void ErrorHandle_ReportFault(u8 vendor_code);
u8 ErrorHandle_TryReset(void);
void LedError(u8 ErrNum);

/* The first detected fault is latched until a safe CiA 402 fault reset. */
extern volatile u8 ErrNumFlag;
extern u16 u16VendorErrorCode;
extern u16 u16ErrorRegister;

#endif
