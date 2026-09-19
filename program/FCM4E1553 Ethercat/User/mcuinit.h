#ifndef MCUINIT_H
#define MCUINIT_H

/* Platform and standard-library declarations. */
#include "N32G45XSYS.h"

/* Application module declarations. */
#include "main.h"
#include "AdcInit.h"
#include "PwmInit.h"
#include "IoIntInit.h"
//#include "SciInit.h"
#include "can.h"
#include "eeprom.h"
#include "eeprom_console.h"
#include "exti.h"
#include "Flash.h"
#include "n32g45x_it.h"
#include "qspi.h"
#include "rs485.h"
#include "timer.h"
#include "u_i2c.h"
#include "uartc.h"
#include "MotionPID.h"
#include "PulseHandle.h"
#include "Errorhadle.h"
#include "SaveFunc.h"

#define PWMPRD	2250	

/* Existing project services retained from the current project. */
#include "systick.h"
#include "sys.h"
#include "modbus.h"

/* EtherCAT and CiA402 declarations used by the application layer. */
#include "applInterface.h"
#include "cia402appl.h"
#include "ecat_def.h"
#include "el9800hw.h"

/* STD245S exposed the raw motor-fault input through this macro. */
#ifndef MF
#define MF IoMfGet()
#endif

#endif
