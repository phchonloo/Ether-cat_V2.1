#ifndef _AdcInit_H_
#define _AdcInit_H_

#include "mcuinit.h"

void adc_config(void);
void dma_config(void);

extern u32 adc_value[10];

#endif
