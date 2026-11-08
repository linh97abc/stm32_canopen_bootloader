/**
 * Driver flash STM32G4 mức thanh ghi (LL không có API ghi/xóa flash).
 * Code chạy từ flash: CPU bị stall trong lúc ghi/xóa, điều này được chấp nhận
 * (design.md mục 4).
 */
#include "blc/blc_flash.h"

#include "blc/blc_layout.h"
#include "stm32g4xx.h"

#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL

#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
                         FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static volatile uint32_t s_ecc_events;

uint32_t blc_flash_ecc_events(void) { return s_ecc_events; }

bool blc_flash_nmi_handler(void)
{
    if ((FLASH->ECCR & FLASH_ECCR_ECCD) != 0u) {
        /* ECCD là rc_w1. Giữ nguyên ECCCIE, không xóa nhầm ECCC. */
        FLASH->ECCR = (FLASH->ECCR & ~FLASH_ECCR_ECCC) | FLASH_ECCR_ECCD;
        s_ecc_events++;
        return true;
    }
    return false;
}

bool blc_flash_read_dw(uint32_t addr, uint64_t *out)
{
    uint32_t before = s_ecc_events;
    uint32_t lo = *(volatile const uint32_t *)addr;
    uint32_t hi = *(volatile const uint32_t *)(addr + 4u);
    __DSB();
    __ISB();
    *out = ((uint64_t)hi << 32) | lo;
    return s_ecc_events == before;
}

static bool flash_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        IWDG->KR = 0xAAAAu; /* vô hại nếu IWDG chưa chạy */
    }
    uint32_t sr = FLASH->SR;
    FLASH->SR = sr & (FLASH_SR_ERRORS | FLASH_SR_EOP);
    return (sr & FLASH_SR_ERRORS) == 0u;
}

static bool flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        return false;
    }
    (void)flash_wait();               /* chờ thao tác trước, xóa cờ lỗi cũ */
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
    return true;
}

static void flash_lock(void) { FLASH->CR |= FLASH_CR_LOCK; }

static void flash_flush_caches(void)
{
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR = acr;
}

bool blc_flash_program_dw(uint32_t addr, uint64_t value)
{
    if ((addr & 7u) != 0u || !flash_unlock()) {
        return false;
    }
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint32_t *)addr = (uint32_t)value;
    __ISB();
    *(volatile uint32_t *)(addr + 4u) = (uint32_t)(value >> 32);
    bool ok = flash_wait();
    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();
    return ok;
}

bool blc_flash_erase_page(uint32_t addr)
{
    if (addr < BLC_FLASH_BASE || addr >= blc_layout_flash_end() || !blc_layout_check()) {
        return false;
    }
    uint32_t page = (addr - BLC_FLASH_BASE) / BLC_FLASH_PAGE_SIZE;
    if (!flash_unlock()) {
        return false;
    }
    uint32_t cr = FLASH->CR & ~(FLASH_CR_PNB_Msk | FLASH_CR_PG | FLASH_CR_MER1);
#ifdef FLASH_CR_BKER
    cr &= ~FLASH_CR_BKER; /* single-bank: luôn bank 1 */
#endif
    FLASH->CR = cr | FLASH_CR_PER | ((page << FLASH_CR_PNB_Pos) & FLASH_CR_PNB_Msk);
    FLASH->CR |= FLASH_CR_STRT;
    bool ok = flash_wait();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_Msk);
    flash_lock();
    flash_flush_caches();
    return ok;
}
