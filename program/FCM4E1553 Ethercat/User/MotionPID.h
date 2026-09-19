#ifndef _MotionPID_H_
#define _MotionPID_H_

#include "mcuinit.h"


void shuzu_initial(void);
void PhaseGet(s16 inputrpm);
void CurrentCal(void);
void MotorWork(s32 inputIa,s32 inputIb );
void MotorFunc(void);
long LimitAm(long indata,long am);
s32 PassLow(s32 ts,s32 indata[],s32 outdata[]);
void MainFunt(void);

void ad_inital(void);


extern s32 CountFlag;
extern s32 Iangle,xifen;
extern long Am;

extern u8 ready_ad,EnaFlag;

extern s32 TestSpeed,SpeedStar,WorkSpeedMax;


#endif
