  
/*--------------------------------------------------------------------------------------
------
------    Includes
------
--------------------------------------------------------------------------------------*/
#include "ecat_def.h"
#if EL9800_HW
#include "qspi.h"
#include "ecatslv.h"

#define    _EL9800HW_ 1
#include "el9800hw.h"

/* Sink for legacy LED1/LED3/LED5 PDO writes; the real pins belong to RS485. */
volatile uint8_t g_rs485_reserved_led_sink;
#undef    _EL9800HW_
/* ECATCHANGE_START(V5.11) ECAT10*/
/*remove definition of _EL9800HW_ (#ifdef is used in el9800hw.h)*/
/* ECATCHANGE_END(V5.11) ECAT10*/

#include "ecatappl.h"
#include "delay.h"



/*--------------------------------------------------------------------------------------
------
------    internal Types and Defines
------
--------------------------------------------------------------------------------------*/

typedef union
{
    unsigned short    Word;
    unsigned char    Byte[2];
} UBYTETOWORD;

typedef union 
{
    UINT8           Byte[2];
    UINT16          Word;
}
UALEVENT;

/*-----------------------------------------------------------------------------------------
------
------    SPI defines/macros
------
-----------------------------------------------------------------------------------------*/
#define SPI_DEACTIVE                    1
#define SPI_ACTIVE                        0


#if INTERRUPTS_SUPPORTED
/*-----------------------------------------------------------------------------------------
------
------    Global Interrupt setting
------
-----------------------------------------------------------------------------------------*/

#define 	DISABLE_GLOBAL_INT           			  __disable_irq()
#define 	ENABLE_GLOBAL_INT           		    __enable_irq()

/*
 * NVIC分组2下，抢占优先级1对应BASEPRI值0x40。
 * EtherCAT/QSPI临界区只屏蔽优先级1及以下的通信中断，
 * 保留优先级0的TIM8和ADC DMA中断，避免ADC中断响应抖动。
 */
#define    ECAT_CRITICAL_BASEPRI          ((uint32_t)0x40U)
#define    DISABLE_AL_EVENT_INT           __set_BASEPRI(ECAT_CRITICAL_BASEPRI)
#define    ENABLE_AL_EVENT_INT            __set_BASEPRI(0U)



/*-----------------------------------------------------------------------------------------
------
------    ESC Interrupt
------
-----------------------------------------------------------------------------------------*/
#if AL_EVENT_ENABLED
#define    INIT_ESC_INT           exti_init_irq();					
#define    EcatIsr                EXTI0_IRQHandler
#define    ACK_ESC_INT         		EXTI_ClrITPendBit(EXTI_LINE0);  
#define IS_ESC_INT_ACTIVE					 
#endif //#if AL_EVENT_ENABLED


/*-----------------------------------------------------------------------------------------
------
------    SYNC0 Interrupt
------
-----------------------------------------------------------------------------------------*/
#if DC_SUPPORTED && _STM32_IO8
#define    INIT_SYNC0_INT                 exti_init_sync0();		
#define    Sync0Isr                       EXTI1_IRQHandler 
#define    DISABLE_SYNC0_INT              NVIC_DisableIRQ(EXTI1_IRQn);	 
#define    ENABLE_SYNC0_INT               NVIC_EnableIRQ(EXTI1_IRQn);	
#define    ACK_SYNC0_INT                  EXTI_ClrITPendBit(EXTI_LINE1);
#define    IS_SYNC0_INT_ACTIVE             

																					
/*ECATCHANGE_START(V5.10) HW3*/

#define    INIT_SYNC1_INT                  exti_init_sync1();
#define    Sync1Isr                        EXTI2_IRQHandler
#define    DISABLE_SYNC1_INT               NVIC_DisableIRQ(EXTI2_IRQn);
#define    ENABLE_SYNC1_INT                NVIC_EnableIRQ(EXTI2_IRQn); 
#define    ACK_SYNC1_INT                   EXTI_ClrITPendBit(EXTI_LINE2);
#define    IS_SYNC1_INT_ACTIVE              

/*ECATCHANGE_END(V5.10) HW3*/

#endif //#if DC_SUPPORTED && _STM32_IO8

#endif	//#if INTERRUPTS_SUPPORTED
/*-----------------------------------------------------------------------------------------
------
------    Hardware timer
------
-----------------------------------------------------------------------------------------*/
#if _STM32_IO8
#if ECAT_TIMER_INT
#define ECAT_TIMER_INT_STATE       
#define ECAT_TIMER_ACK_INT        		 	TIM_ClrIntPendingBit(TIM6 , TIM_FLAG_UPDATE);	
#define    TimerIsr                     TIM6_IRQHandler					
#define    ENABLE_ECAT_TIMER_INT        NVIC_EnableIRQ(TIM6_IRQn) ;	
#define    DISABLE_ECAT_TIMER_INT       NVIC_DisableIRQ(TIM6_IRQn) ;

#define INIT_ECAT_TIMER           			timer_init(10) ;   

#define STOP_ECAT_TIMER            			DISABLE_ECAT_TIMER_INT;/*disable timer interrupt*/ \

#define START_ECAT_TIMER          			ENABLE_ECAT_TIMER_INT


#else    //#if ECAT_TIMER_INT

#define INIT_ECAT_TIMER      					timer_init(10);

#define STOP_ECAT_TIMER              TIM_Cmd(TIM6, DISABLE);		 

#define START_ECAT_TIMER              TIM_Cmd(TIM6, ENABLE);   			

#endif //#else #if ECAT_TIMER_INT

#elif _STM32_IO4

#if !ECAT_TIMER_INT
#define    ENABLE_ECAT_TIMER_INT       NVIC_EnableIRQ(TIM6_IRQn) ;	
#define    DISABLE_ECAT_TIMER_INT      NVIC_DisableIRQ(TIM6_IRQn) ;
#define INIT_ECAT_TIMER               timer_init(10) ;	
#define STOP_ECAT_TIMER              	TIM_Cmd(TIM6, DISABLE);	
#define START_ECAT_TIMER           		TIM_Cmd(TIM6, ENABLE);			

#else    //#if !ECAT_TIMER_INT

#warning "define Timer Interrupt Macros"

#endif //#else #if !ECAT_TIMER_INT
#endif //#elif _STM32_IO4

/*-----------------------------------------------------------------------------------------
------
------    Configuration Bits
------
-----------------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------------
------
------    LED defines
------
-----------------------------------------------------------------------------------------*/
#if _STM32_IO8
// EtherCAT Status LEDs -> StateMachine
#define LED_ECATGREEN               
#define LED_ECATRED                   
#endif //_STM32_IO8


/*--------------------------------------------------------------------------------------
------
------    internal Variables
------
--------------------------------------------------------------------------------------*/
UALEVENT         EscALEvent;            //contains the content of the ALEvent register (0x220), this variable is updated on each Access to the Esc

#define ESC_PDI_INIT_RETRY_LIMIT (10000U)

/* Debugger-visible PDI bring-up diagnostics.
   stage: 1=BYTE_ORDER, 2=CHIP_ID, 3=AL_EVENT_MASK, 4=IRQ config, 0x80=ready. */
volatile UINT32 g_esc_pdi_init_stage = 0U;
volatile UINT32 g_esc_pdi_last_value = 0U;
volatile UINT8 g_esc_pdi_init_error = 0U;

/*--------------------------------------------------------------------------------------
------
------    internal functions
------
--------------------------------------------------------------------------------------*/


/*******************************************************************************
  Function:
    void GetInterruptRegister(void)

  Summary:
    The function operates a SPI access without addressing.

  Description:
    The first two bytes of an access to the EtherCAT ASIC always deliver the AL_Event register (0x220).
    It will be saved in the global "EscALEvent"
  *****************************************************************************/
static void GetInterruptRegister(void)
{
      DISABLE_AL_EVENT_INT;
      HW_EscReadIsr((MEM_ADDR *)&EscALEvent.Word, 0x220, 2);
      ENABLE_AL_EVENT_INT;

}


/*******************************************************************************
  Function:
    void ISR_GetInterruptRegister(void)

  Summary:
    The function operates a SPI access without addressing.
        Shall be implemented if interrupts are supported else this function is equal to "GetInterruptRegsiter()"

  Description:
    The first two bytes of an access to the EtherCAT ASIC always deliver the AL_Event register (0x220).
        It will be saved in the global "EscALEvent"
  *****************************************************************************/

static void ISR_GetInterruptRegister(void)
{
    HW_EscReadIsr((MEM_ADDR *)&EscALEvent.Word, 0x220, 2);
}

/*--------------------------------------------------------------------------------------
------
------    exported hardware access functions
------
--------------------------------------------------------------------------------------*/
/*******************************************************************************
* Function Name  : GPIO_Config
* Description    : init the led and swtich port
* Input          : None
* Output         : None
* Return         : None
* Attention		 : None
*******************************************************************************/
void GPIO_Config(void) 
{ 
	GPIO_InitType GPIO_InitStructure;
	RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB |RCC_APB2_PERIPH_GPIOC, ENABLE);	
	
//----------------------------------------------------------------switch
//    SWITCH_1    --- PA8
//    SWITCH_2    --- PA11
//    SWITCH_3    --- PC7
//    SWITCH_4    --- PC8
//    SWITCH_5    --- PC6
//    SWITCH_6    --- PC11
//    SWITCH_7    --- PC12
//    SWITCH_8    --- PA12
	
	
	/* PA8 is the active-low motor ERROR LED and PA11/PA12 are CAN1.
	   Their peripheral drivers configure the pins after HW_Init(), so the
	   legacy switch setup must not claim them during ESC initialization. */

	/* PC6～PC9由TIM8 PWM使用，PC12由板级Y3输出使用。 */
	
//-----------------------------------------------------------------led
//    LED_1    --- PB8
//    LED_2    --- PB6
//    LED_3    --- PB9
//    LED_4    --- PB3
//    LED_5    --- PC13
//    LED_6    --- PC14
//    LED_7    --- PC15
//    LED_8    --- PB14
	


	/* PC13 is the RS485 DE pin; only PC14/PC15 remain EtherCAT LEDs. */
	GPIO_InitStructure.Pin = GPIO_PIN_14 | GPIO_PIN_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitPeripheral(GPIOC, &GPIO_InitStructure);



  /* PC6/PC7/PC8/PC9 are configured later by PwmInit.c as TIM8 hardware PWM.
     PB14 is the active-low VFO hardware over-current input. Never drive it:
     Q9 on the driver board also pulls this net low during a fault. */
  GPIO_InitStructure.Pin = GPIO_PIN_14;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitPeripheral(GPIOB, &GPIO_InitStructure);


} 
 


/*
 * 描    述：FCM4E1553_reset
 * 功    能：复位FCM4E1553此芯片
 * 入口参数：无
 * 出口参数：无
 */ 
#ifndef FCM4E1553_RESET_ON_PA0
#define FCM4E1553_RESET_ON_PA0 0
#endif

void FCM4E1553_reset(void)
{		
#if FCM4E1553_RESET_ON_PA0
	GPIO_InitType  GPIO_InitStructure;

	RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);

	GPIO_InitStructure.Pin = GPIO_PIN_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure); 
 
 
	{
		GPIO_WriteBit(GPIOA, GPIO_PIN_0, Bit_RESET);
		delay_ms(1);
		GPIO_WriteBit(GPIOA, GPIO_PIN_0, Bit_SET);
		
		delay_ms(1);
	}
#else
	/* PA0 is the opto-isolated X1 input on the V1.2 board. ESC reset is
	   handled in hardware; never drive PA0 as an ESC reset output. */
	delay_ms(2);
#endif
} 
/////////////////////////////////////////////////////////////////////////////////////////
/**
\return     0 if initialization was successful

 \brief    This function intialize the Process Data Interface (PDI) and the host controller.
*////////////////////////////////////////////////////////////////////////////////////////
 UINT8 HW_Init(void)
{
	UINT32 intMask;
	UINT32 data;
	UINT32 Chip_ID;
	UINT32 retry;

	g_esc_pdi_init_stage = 0U;
	g_esc_pdi_last_value = 0U;
	g_esc_pdi_init_error = 0U;
	
	GPIO_Config();
	
	FCM4E1553_reset();
	
	QSPI_GPIO(QSPI_AFIO_PORT_SEL, 0, 0);
	// qspi_init(STANDARD_SPI_FORMAT_SEL, RX_ONLY, 32);
	/* initialize the SSP registers for the ESC SPI */
	
		
   //Read BYTE-ORDER register 0x3064.
  g_esc_pdi_init_stage = 1U;
  retry = ESC_PDI_INIT_RETRY_LIMIT;
  do
  {
		qspi_init(STANDARD_SPI_FORMAT_SEL, RX_ONLY, 32);
		QspiSendWord(0x38);
     	HW_EscReadDWord(data,0x3064);
		g_esc_pdi_last_value = data;
  }while((0x87654321 != data) && (--retry > 0U));
	if (retry == 0U)
	{
		g_esc_pdi_init_error = 1U;
		return g_esc_pdi_init_error;
	}
	
	
	g_esc_pdi_init_stage = 2U;
	retry = ESC_PDI_INIT_RETRY_LIMIT;
	do
  {
     	HW_EscReadDWord(Chip_ID,0x3050);
		g_esc_pdi_last_value = Chip_ID;
  }while((0x13530000 != Chip_ID) && (--retry > 0U));
	if (retry == 0U)
	{
		g_esc_pdi_init_error = 2U;
		return g_esc_pdi_init_error;
	}
	
	g_esc_pdi_init_stage = 3U;
	retry = ESC_PDI_INIT_RETRY_LIMIT;
	do
	{
			intMask = 0x0093;
			HW_EscWriteDWord(intMask, ESC_AL_EVENTMASK_OFFSET);
			intMask = 0;
			HW_EscReadDWord(intMask, ESC_AL_EVENTMASK_OFFSET);
			g_esc_pdi_last_value = intMask;
	} while ((intMask!= 0x0093) && (--retry > 0U));
	if (retry == 0U)
	{
		g_esc_pdi_init_error = 3U;
		return g_esc_pdi_init_error;
	}
		

		
		//IRQ enable,IRQ polarity, IRQ buffer type in Interrupt Configuration register.
		g_esc_pdi_init_stage = 4U;
    //Wrte 0x54 - 0x00000101
    data = 0x00000101;
    HW_EscWriteDWord(data,0x3054);
    intMask = 0;	
    HW_EscReadDWord(intMask,0x3054);	
    
    //Write in Interrupt Enable register -->
    //Write 0x5c - 0x00000001
    data = 0x00000001;
    HW_EscWriteDWord(data,0x305C);
    intMask = 0;	
    HW_EscReadDWord(intMask,0x305C);	
	
    intMask = 0;
		HW_EscReadDWord(intMask,0x3058);
		
		intMask = 0x00;	  
    HW_EscWriteDWord(intMask, ESC_AL_EVENTMASK_OFFSET);
		
			

#if AL_EVENT_ENABLED



    INIT_ESC_INT;
    ENABLE_ESC_INT();
#endif

#if DC_SUPPORTED&& _STM32_IO8
    INIT_SYNC0_INT
    INIT_SYNC1_INT

    ENABLE_SYNC0_INT;
    ENABLE_SYNC1_INT;
#endif

    INIT_ECAT_TIMER;
    START_ECAT_TIMER;
  
	
#if INTERRUPTS_SUPPORTED
    /* enable all interrupts */
    ENABLE_GLOBAL_INT;
#endif

		g_esc_pdi_init_stage = 0x80U;
    return 0;
}



/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    This function shall be implemented if hardware resources need to be release
        when the sample application stops
*////////////////////////////////////////////////////////////////////////////////////////
void HW_Release(void)
{

}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    first two Bytes of ALEvent register (0x220)

 \brief  This function gets the current content of ALEvent register
*////////////////////////////////////////////////////////////////////////////////////////
UINT16 HW_GetALEventRegister(void)
{
    GetInterruptRegister();
    return EscALEvent.Word;
}
#if INTERRUPTS_SUPPORTED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    first two Bytes of ALEvent register (0x220)

 \brief  The SPI PDI requires an extra ESC read access functions from interrupts service routines.
        The behaviour is equal to "HW_GetALEventRegister()"
*////////////////////////////////////////////////////////////////////////////////////////
#if _STM32_IO4  && AL_EVENT_ENABLED
/* the pragma interrupt_level is used to tell the compiler that these functions will not
   be called at the same time from the main function and the interrupt routine */
//#pragma interrupt_level 1
#endif
UINT16 HW_GetALEventRegister_Isr(void)
{
     ISR_GetInterruptRegister();
    return EscALEvent.Word;
}
#endif


#if UC_SET_ECAT_LED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param RunLed            desired EtherCAT Run led state
 \param ErrLed            desired EtherCAT Error led state

  \brief    This function updates the EtherCAT run and error led
*////////////////////////////////////////////////////////////////////////////////////////
void HW_SetLed(UINT8 RunLed,UINT8 ErrLed)
{
#if _STM32_IO8
 //     LED_ECATGREEN = RunLed;
//      LED_ECATRED   = ErrLed;
#endif
}
#endif //#if UC_SET_ECAT_LED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param pData        Pointer to a byte array which holds data to write or saves read data.
 \param Address     EtherCAT ASIC address ( upper limit is 0x1FFF )    for access.
 \param Len            Access size in Bytes.

 \brief  This function operates the SPI read access to the EtherCAT ASIC.
*////////////////////////////////////////////////////////////////////////////////////////
void HW_EscRead( MEM_ADDR *pData, UINT16 Address, UINT16 Len )
{
	UINT16 len = Address & 0x3;
	UINT16 num;
	UINT16 Length=Len;
	UINT8 *pTmpData = (UINT8 *)pData;
	uint8_t RxBuff[300];
	uint8_t *ptr = RxBuff;


	DISABLE_AL_EVENT_INT;
	while(Len > 0)
	{
			num= (Len > 4) ? 4 : Len;
			
			if((Address & 01) && (Address & 02))
			{
				Address -= 3;
				num=1;
			}
			else if(Address & 01)
			{
				Address -= 1;
				num=1;
			} 
			else if (Address & 02)
			{
				Address -= 2;
				num= (num&1) ? 1:2;
			} 
		
		
		qspi_read(ptr,Address,4);
		
		
		Len -= num;
    ptr += 4;
    Address += 4;
	}

	for(int i=0;i<Length;i++)
	{
		pTmpData[i] = RxBuff[len+i];

	}
	ENABLE_AL_EVENT_INT;

}
#if INTERRUPTS_SUPPORTED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param pData        Pointer to a byte array which holds data to write or saves read data.
 \param Address     EtherCAT ASIC address ( upper limit is 0x1FFF )    for access.
 \param Len            Access size in Bytes.

\brief  The SPI PDI requires an extra ESC read access functions from interrupts service routines.
        The behaviour is equal to "HW_EscRead()"
*////////////////////////////////////////////////////////////////////////////////////////
#if _STM32_IO4  && AL_EVENT_ENABLED
/* the pragma interrupt_level is used to tell the compiler that these functions will not
   be called at the same time from the main function and the interrupt routine */
//#pragma interrupt_level 1
#endif
 void HW_EscReadIsr( MEM_ADDR *pData, UINT16 Address, UINT16 Len )
{
	UINT16 len = Address & 0x3;
	UINT16 num;
	UINT16 Length=Len;
	UINT8 *pTmpData = (UINT8 *)pData;
	uint8_t RxBuff[300];
	uint8_t *ptr = RxBuff;

	while(Len > 0)
	{
			num= (Len > 4) ? 4 : Len;
			
			if((Address & 01) && (Address & 02))
			{
				Address -= 3;
				num=1;
			}
			else if(Address & 01)
			{
				Address -= 1;
				num=1;
			} 
			else if (Address & 02)
			{
				Address -= 2;
				num= (num&1) ? 1:2;
			} 
		
		
		qspi_read(ptr,Address,4);
		
		
	Len -= num;
    ptr += 4;
    Address += 4;
	}

	for(int i=0;i<Length;i++)
	{
		pTmpData[i] = RxBuff[len+i];

	}

}
#endif //#if INTERRUPTS_SUPPORTED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param pData        Pointer to a byte array which holds data to write or saves write data.
 \param Address     EtherCAT ASIC address ( upper limit is 0x1FFF )    for access.
 \param Len            Access size in Bytes.

  \brief  This function operates the SPI write access to the EtherCAT ASIC.
*////////////////////////////////////////////////////////////////////////////////////////
void HW_EscWrite( MEM_ADDR *pData, UINT16 Address, UINT16 Len )
{
	  UINT16 i;
    UINT8 *pTmpData = (UINT8 *)pData;

    /* loop for all bytes to be written */
    while ( Len )
    {


        i= (Len > 4) ? 4 : Len;

        if(Address & 01)
        {
           i=1;
        }
        else if (Address & 02)
        {
           i= (i&1) ? 1:2;
        }
        else if (i == 03)
        {
            i=1;
        }


        DISABLE_AL_EVENT_INT;
       
        /* start transmission */

        qspi_write(pTmpData, Address, i);


        ENABLE_AL_EVENT_INT;

        /* next address */
        Len -= i;

        pTmpData += i;
        Address += i;
				
    }
	


}
#if INTERRUPTS_SUPPORTED
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param pData        Pointer to a byte array which holds data to write or saves write data.
 \param Address     EtherCAT ASIC address ( upper limit is 0x1FFF )    for access.
 \param Len            Access size in Bytes.

 \brief  The SPI PDI requires an extra ESC write access functions from interrupts service routines.
        The behaviour is equal to "HW_EscWrite()"
*////////////////////////////////////////////////////////////////////////////////////////
void HW_EscWriteIsr( MEM_ADDR *pData, UINT16 Address, UINT16 Len )
{
	  UINT16 i;
    UINT8 *pTmpData = (UINT8 *)pData;

    /* loop for all bytes to be written */
    while ( Len )
    {


        i= (Len > 4) ? 4 : Len;

        if(Address & 01)
        {
           i=1;
        }
        else if (Address & 02)
        {
           i= (i&1) ? 1:2;
        }
        else if (i == 03)
        {
            i=1;
        }

       
        /* start transmission */

        qspi_write(pTmpData, Address, i);

        /* next address */
        Len -= i;

        pTmpData += i;
        Address += i;
				
    }
}

#endif

/*
 * 描述:  EcatIsr
 * 功能:  IRQ中断服务函数
 * 入口:  无
 * 出口:  无
 */
void  EcatIsr(void)
{
   PDI_Isr();

   /* reset the interrupt flag */
   ACK_ESC_INT;
	

}
#endif     // AL_EVENT_ENABLED



#if DC_SUPPORTED&& _STM32_IO8
/*
 * 描述:  Sync0Isr
 * 功能:  SYNC0中断服务函数
 * 入口:  无
 * 出口:  无
 */
void Sync0Isr(void)
{
   DISABLE_ESC_INT();
   Sync0_Isr();

   ACK_SYNC0_INT;
   ENABLE_ESC_INT();
}
/*ECATCHANGE_START(V5.10) HW3*/
/*
 * 描述:  Sync1Isr
 * 功能:  SYNC1中断服务函数
 * 入口:  无
 * 出口:  无
 */
void Sync1Isr(void)
{	
   DISABLE_ESC_INT();
   Sync1_Isr();
	 ACK_SYNC1_INT;
   ENABLE_ESC_INT();

}
/*ECATCHANGE_END(V5.10) HW3*/
#endif

#if _STM32_IO8 && ECAT_TIMER_INT

/*
 * 描述:  TimerIsr
 * 功能:  timer中断服务函数---定时1ms---2000个ticks
 * 入口:  无
 * 出口:  无
 */
void TimerIsr(void)
{		
    DISABLE_ESC_INT();
		ECAT_CheckTimer();
		ECAT_TIMER_ACK_INT;
	  ENABLE_ESC_INT();
}

#endif

//#endif //#if EL9800_HW
/** @} */




