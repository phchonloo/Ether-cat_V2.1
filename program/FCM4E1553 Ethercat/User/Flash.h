#ifndef USER_FLASH_H
#define USER_FLASH_H

#include "mcuinit.h"

#define FlASH_READY_YES (0U)
#define FlASH_READY_NO  (1U)
#define FLASH_READY_YES FlASH_READY_YES
#define FLASH_READY_NO  FlASH_READY_NO

void fmc_erase_pages(void);
void fmc_program(void);
void Flash_ReadInfo(u32 *chI2c_Rxdata, u8 datalen);
void Flash_WriteInfo(u32 *chI2c_Rxdata, u8 datalen);
void Flash_Read_Protect(u8 yesno);

#endif
