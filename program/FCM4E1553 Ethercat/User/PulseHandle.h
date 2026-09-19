#ifndef _PulseHandle_H_
#define _PulseHandle_H_

#include "mcuinit.h"

void DataIni(void);
void PulseFunction(void);
void PuIoIntHandle(void);
void DrIoIntHandle(void);

void PulseSet(void);
void WorkSelf(void);

extern s16 Mean_i, Dir_x;
extern s32 xifen,SpeedNow,BaseRpm;
	
#endif




