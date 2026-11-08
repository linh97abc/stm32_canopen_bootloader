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

void blc_boot_enter_bootloader(void)
{
    __disable_irq();
    (void)bkp_open();
    *bkp_reg() = BLC_BOOT_MAGIC_STAY_IN_BL;
    __DSB();
    NVIC_SystemReset();
}

bool blc_boot_take_request(void)
{
    uint32_t opened = bkp_open();
    bool req = (*bkp_reg() == BLC_BOOT_MAGIC_STAY_IN_BL);
    if (req) {
        *bkp_reg() = 0u;
        __DSB();
    }
    bkp_close(opened);
    return req;
}
