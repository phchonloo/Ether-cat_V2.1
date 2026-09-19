#ifndef N32G45X_SYS_H
#define N32G45X_SYS_H

/*
 * N32G455 platform declarations shared by all application modules.
 * This is the N32 counterpart of STD245S/GD230SYS.h.
 */
#include "n32g45x.h"
#include "n32g45x_dma.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RAMRUN
#define RAMRUN __attribute__((section("RAMCODE")))
#endif

#endif
