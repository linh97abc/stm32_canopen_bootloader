#include "blc/blc_boot.h"

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_pwr.h"

static volatile uint32_t *bkp_reg(void)
{
    return &(&TAMP->BKP0R)[BLC_BOOT_BKP_INDEX];
}

/* Mở quyền ghi backup domain. Trả về mask clock APB1 đã phải bật thêm. */
static uint32_t bkp_open(void)
{
    uint32_t need = LL_APB1_GRP1_PERIPH_PWR | LL_APB1_GRP1_PERIPH_RTCAPB;
    uint32_t was_on = RCC->APB1ENR1 & need;
    LL_APB1_GRP1_EnableClock(need);
    LL_PWR_EnableBkUpAccess();
    while (!LL_PWR_IsEnabledBkUpAccess()) {
    }
    return need & ~was_on;
}

static void bkp_close(uint32_t clocks_turned_on)
{
    LL_PWR_DisableBkUpAccess();
    LL_APB1_GRP1_DisableClock(clocks_turned_on);
}

__attribute__((noreturn)) static void reset_with_magic(uint32_t magic)
{
    __disable_irq();
    (void)bkp_open();
    *bkp_reg() = magic;
    __DSB();
    NVIC_SystemReset();
}

void blc_boot_enter_bootloader(void)
{
    reset_with_magic(BLC_BOOT_MAGIC_STAY_IN_BL);
}

void blc_boot_start_app(void)
{
    reset_with_magic(BLC_BOOT_MAGIC_START_APP);
}

blc_boot_req_t blc_boot_take_request(void)
{
    uint32_t opened = bkp_open();
    uint32_t v = *bkp_reg();
    blc_boot_req_t req = BLC_BOOT_REQ_NONE;
    if (v == BLC_BOOT_MAGIC_STAY_IN_BL) {
        req = BLC_BOOT_REQ_STAY;
    } else if (v == BLC_BOOT_MAGIC_START_APP) {
        req = BLC_BOOT_REQ_START_APP;
    }
    if (req != BLC_BOOT_REQ_NONE) {
        *bkp_reg() = 0u;
        __DSB();
    }
    bkp_close(opened);
    return req;
}
