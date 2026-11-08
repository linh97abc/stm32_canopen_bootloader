#include "pds/pds_layout.h"

#include "stm32g4xx.h"

uint32_t pds_layout_flash_end(void)
{
    uint32_t kb = *(const volatile uint16_t *)FLASHSIZE_BASE;
    return PDS_FLASH_BASE + kb * 1024u;
}

bool pds_layout_check(void)
{
#if PDS_LAYOUT_NEEDS_SINGLE_BANK
    if ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0u) {
        return false; /* dual-bank: page 2 KB, bảng phân vùng 4 KB sẽ sai */
    }
#endif
    return pds_layout_flash_end() > PDS_APP_START;
}
