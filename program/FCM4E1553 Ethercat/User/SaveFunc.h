#ifndef _SaveFunc_H_
#define _SaveFunc_H_

#include "mcuinit.h"

void fmc_erase_pages(void);
void fmc_program(void);
void IniSaveFunc(void);

extern s32 sxifen;
extern   u32 WorkValue[30];
extern u8 WolkMode;	

#endif
