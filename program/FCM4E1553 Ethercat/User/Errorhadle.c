//error.c
#include "Errorhadle.h"

s32 ErrMoudleNum=0,ErrTemNum=0,ErrVolHNum=0,ErrVolLNum=0;
volatile u8 ErrNumFlag=0;
u16 u16VendorErrorCode=0;
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
	{
		/* VFO is one low-active driver protection output.  It does not
		 * identify an open motor phase or a DC-bus voltage fault. */
		ErrorHandle_ReportFault(10U);
	}



//	if(ErrNumFlag>0)
//		LedError(ErrNumFlag);


}

void ErrorHandle_ReportFault(u8 vendor_code)
{
	/* Codes from the iD256-EC manual.  Other detectors may use this entry
	 * point only after their physical measurement has been validated. */
	if ((vendor_code != 10U) && (vendor_code != 11U) &&
	    (vendor_code != 13U) && (vendor_code != 14U))
	{
		return;
	}
	if (ErrNumFlag == 0U)
	{
		ErrNumFlag = vendor_code;
		ready_ad = 0U;
		UpPwm(0U, 0U, 0U, 0U);
	}
}

u8 ErrorHandle_TryReset(void)
{
	/* Do not acknowledge an asserted or recently asserted VFO input. */
	if ((IoVfoGet() == 0U) || (ErrI_sum != 16))
	{
		return 0U;
	}
	ErrNumFlag = 0U;
	u16VendorErrorCode = 0U;
	u16ErrorRegister = 0U;
	ErrFlag = 0U;
	return 1U;
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


