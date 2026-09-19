#ifndef _GD230SYS_H_
#define _GD230SYS_H_

/*
 * STD245S compatibility header.
 *
 * The reference project uses a GD32E230 device.  This project runs on the
 * N32G455, so the original GD32 device headers must not be pulled in here.
 * Keep the public filename used by the motor-control sources and expose the
 * equivalent N32 platform declarations instead.
 */
#include "N32G45XSYS.h"
#include "systick.h"

#endif
