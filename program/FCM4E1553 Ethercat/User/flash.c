#include "Flash.h"

/* N32工程使用256 KiB IROM；保留最后一个2 KiB页存放电机参数。 */
#define PARAM_FLASH_ADDR      ((u32)0x0803F800U)
#define PARAM_FLASH_PAGE_SIZE ((u32)0x00000800U)
#define PARAM_FLASH_WORDS     (PARAM_FLASH_PAGE_SIZE / sizeof(u32))

static const u32 s_flash_test_word = 0x01234567U;

void fmc_erase_pages(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    (void)FLASH_EraseOnePage(PARAM_FLASH_ADDR);
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    FLASH_Lock();
}

void fmc_program(void)
{
    u32 address;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    for (address = PARAM_FLASH_ADDR;
         address < (PARAM_FLASH_ADDR + PARAM_FLASH_PAGE_SIZE);
         address += sizeof(u32))
    {
        (void)FLASH_ProgramWord(address, s_flash_test_word);
        FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    }
    FLASH_Lock();
}

void Flash_ReadInfo(u32 *chI2c_Rxdata, u8 datalen)
{
    const u32 *source;
    u32 index;

    if (chI2c_Rxdata == NULL)
    {
        return;
    }
    source = (const u32 *)PARAM_FLASH_ADDR;
    for (index = 0U; index < datalen; index++)
    {
        chI2c_Rxdata[index] = source[index];
    }
}

void Flash_WriteInfo(u32 *chI2c_Rxdata, u8 datalen)
{
    u32 address;
    u32 index;

    if ((chI2c_Rxdata == NULL) || ((u32)datalen > PARAM_FLASH_WORDS))
    {
        return;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    if (FLASH_EraseOnePage(PARAM_FLASH_ADDR) == FLASH_COMPL)
    {
        address = PARAM_FLASH_ADDR;
        for (index = 0U; index < datalen; index++)
        {
            if (FLASH_ProgramWord(address, chI2c_Rxdata[index]) != FLASH_COMPL)
            {
                break;
            }
            address += sizeof(u32);
        }
    }
    FLASH_ClearFlag(FLASH_STS_CLRFLAG);
    FLASH_Lock();
}

void Flash_Read_Protect(u8 yesno)
{
    FlagStatus protected_state;

    protected_state = FLASH_GetReadOutProtectionSTS();
    if (((yesno != 0U) && (protected_state == RESET))
        || ((yesno == 0U) && (protected_state != RESET)))
    {
        FLASH_Unlock();
        (void)FLASH_ReadOutProtectionL1(
            (yesno != 0U) ? ENABLE : DISABLE);
        FLASH_Lock();
    }
}
