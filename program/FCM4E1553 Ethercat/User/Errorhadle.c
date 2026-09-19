//error.c
#include "Errorhadle.h"

s32 ErrMoudleNum=0,ErrTemNum=0,ErrVolHNum=0,ErrVolLNum=0;
u8 	ErrNumFlag=0;
u8 	ErrFlag=0;
u32 LedErrorC=0; 			//一个周期内闪烁次数的倒数(一次闪烁时间)
u32 LedPerriodTime_2=0;		//闪烁周期变量
u8 LedError_3=0,LedMin=0;

s8 ErrI[16]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},ErrI_i=15,ErrI_sum=16;



void ErrorHandle(void)
{
	//----------判断模块是否保护----------------------------------

	ErrI_sum-=ErrI[ErrI_i];
	ErrI[ErrI_i]=IoVfoGet();//GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12);	3过流保护
 	ErrI_sum+=ErrI[ErrI_i];
	ErrI_i++;
	if(ErrI_i>15)
		ErrI_i=0;
	if(ErrI_sum<=2)
	{	ErrNumFlag=4;
		ready_ad=0;
//		WorkValue[4]=ErrI_sum;
	}		



//	if(ErrNumFlag>0)
//		LedError(ErrNumFlag);


}


void LedError(u8 ErrNum)
{
	LedPerriodTime_2++;
	LedErrorC++;
	ErrFlag=1;
	LedRunSet(1);//RUNOFF;
	UpPwm(0,0,0,0);

 	if(LedPerriodTime_2>=32000)//33000
 	{	LedPerriodTime_2=0;
  		if(LedError_3==0)
			LedError_3=1;
  		else
			LedError_3=0;
 	}
 	if(LedError_3==0)		  
 	{
 	 	if(LedErrorC>=16000/ErrNum)//16500
		{
			LedErrSet(2);//gpio_bit_toggle(GPIOB,GPIO_PIN_11);
			LedErrorC=0;
		}
 	}
 	else
	{	LedErrSet(1);//ERROFF;
	}

}


