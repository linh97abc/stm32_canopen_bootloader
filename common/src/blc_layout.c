#include "blc/blc_layout.h"

#include "stm32g4xx.h"

uint32_t blc_layout_flash_end(void)
{
    uint32_t kb = *(const volatile uint16_t *)FLASHSIZE_BASE;
    return BLC_FLASH_BASE + kb * 1024u;
}

bool blc_layout_check(void)
{
#if BLC_LAYOUT_NEEDS_SINGLE_BANK
    if ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0u) {
        return false; /* dual-bank: page 2 KB, bảng phân vùng 4 KB sẽ sai */
    }
#endif
    return blc_layout_flash_end() > BLC_APP_START;
}
