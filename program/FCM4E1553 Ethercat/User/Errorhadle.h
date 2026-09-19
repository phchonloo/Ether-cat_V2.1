#ifndef _ERRORHADLE_H_
#define _ERRORHADLE_H_

#include "mcuinit.h"


void ErrorHandle(void);
void LedError(u8 ErrNum);

extern u8 	ErrNumFlag;

#endif
