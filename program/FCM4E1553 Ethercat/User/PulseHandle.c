#include "PulseHandle.h"



s16 PuCount[2]={0,0},DrCount[2]={0,0},DrPulseNum=0,PuPulseNum=0,PU_add=0,DR_add=0,SumPulsePu=0,SumPulseDr=0;
//s32 SpeedCap[300],SpeedCap_i=0;
s16 Dir_x=1;
s32 SumStep=0,SpeedNow=0,BaseRpm=0;
s16 Mean_i=1;
s32 xifen=1000;

s32 TestSpeed=0,SpeedStar=1,WorkSpeedMax=1;
s32 SpeedAdd,SpeedIndex,SpeedMin,SpeedStar,gongzuo_shiji,gongzuo_half,gongzuo_stop;

u8 SumPu=0,SumDr=0,PuIo[8]={0,0,0,0,0,0,0,0},DrIo[8]={0,0,0,0,0,0,0,0},Ioi=0,PuIoF=0,DrIoF=0,RunF=0,PuIoF1=0,DrIoF1=0;
	
/* 位置/速度环：把 PU/DR 脉冲计数换算成 BaseRpm（每 16kHz 节拍调用一次） */
void PulseFunction(void)
{	long CapSpeed=0;



 	BaseRpm=480;

}
void RAMRUN  WorkSelf(void)
{
	s32 SpeedIndex,SpeedAcc;
	s32 SpeedLow,WorkSpeedMax1,Spin;

	SpeedIndex=XiFen();
	SpeedLow=WorkValue[9]<<4;
	SpeedAcc=WorkValue[8]<<4;
	SpeedStar=WorkValue[6]<<4;
	WorkSpeedMax=SpeedLow+SpeedAcc*SpeedIndex;

	Spin=adc_value[2];

	if(Spin<WorkValue[10])
		Spin=0;

	if((PuIoF==0&&WorkValue[4]==0)||(WorkValue[4]==1&&DrIoF==0&&PuIoF==0))
	{
		TestSpeed-=WorkValue[7];
		if(TestSpeed<=SpeedStar)
		{	TestSpeed=0;

		}

	}
	else
	{
		if(DrIoF==1)
			Dir_x=-1;
		else
			Dir_x=1;

		if(TestSpeed>WorkSpeedMax)
			TestSpeed-=WorkValue[7];
		else if(TestSpeed<WorkSpeedMax)
			TestSpeed+=WorkValue[7];

	}

 	BaseRpm=TestSpeed*Dir_x;


}

/* WolkMode>2：给定脉冲数走一段固定行程 */
void RAMRUN PulseSet(void)
{	u32 PulseN,tempulse;
	u64 tempulses;


	if(PuIoF1==0&&PuIoF==1&&TestSpeed==0)
	{
		RunF=1;
		Dir_x=1;
	}
	else if(DrIoF1==0&&DrIoF==1&&TestSpeed==0)
	{
		RunF=1;
		Dir_x=-1;

	}

	if(RunF==1)
	{
		PulseN=15360000/sxifen;
		tempulse=WorkValue[9];
		tempulses=(tempulse*10000)+WorkValue[10];
		gongzuo_shiji=tempulses*PulseN;
		gongzuo_half=gongzuo_shiji>>1;
		gongzuo_stop=0;
		WorkSpeedMax=WorkValue[8]<<4;
		SpeedStar=WorkValue[6]<<4;
		RunF=2;

	}
	else if(RunF==2)		/* 加速段 */
	{	if(TestSpeed<WorkSpeedMax&&gongzuo_stop<gongzuo_half)
		{	TestSpeed+=WorkValue[7];
			gongzuo_stop+=TestSpeed;
		}
		else
		{	RunF=3;

		}

	}
	else if(RunF==3&&gongzuo_shiji<=gongzuo_stop)	/* 减速点 */
	{
			RunF=4;

	}
	else if(RunF==4)	/* 减速段 */
	{
		if(TestSpeed>SpeedStar)
			TestSpeed-=WorkValue[7];
		if(gongzuo_shiji<=TestSpeed)
		{	TestSpeed=gongzuo_shiji;

		}
		if(gongzuo_shiji<=0)
		{	RunF=0;
			TestSpeed=0;
		}
	}

	BaseRpm=TestSpeed*Dir_x;
	gongzuo_shiji-=TestSpeed;


}
/* PA0 脉冲中断：WolkMode==0 时靠 DIR(PA1) 电平判定方向 */
void PuIoIntHandle(void)
{

	;

}

/* PA1 方向脉冲中断：只有 WolkMode==1（双脉冲模式）才计数 */
void DrIoIntHandle(void)
{
	;

}

void DataIni(void)
{   u16 i;
   ;

}


