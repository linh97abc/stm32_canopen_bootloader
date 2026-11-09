#include "clock.h"

#include "board.h"
#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_pwr.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_system.h"

uint32_t SystemCoreClock = 16000000u; /* CMSIS, thay cho system_stm32g4xx.c */

static volatile uint32_t s_ms;
static uint32_t s_fdcan_hz;
static bool s_hse_started;
static uint32_t s_hse_t0;

void SysTick_Handler(void) { s_ms++; }

uint32_t clock_ms(void) { return s_ms; }

void watchdog_kick(void) { IWDG->KR = 0xAAAAu; }

static void delay_cycles(uint32_t n)
{
    while (n--) {
        __NOP();
    }
}

void clock_hse_start(void)
{
    if (s_hse_started) {
        return;
    }
#if BOARD_HSE_BYPASS
    LL_RCC_HSE_EnableBypass();
#endif
    LL_RCC_HSE_Enable();
    s_hse_t0 = s_ms;
    s_hse_started = true;
}

/* Chờ HSE ổn định, giới hạn tính từ lúc bật: thời gian đã trôi (kiểm tra app)
 * được trừ vào. Cần SysTick đang chạy (s_ms tăng). */
static bool hse_wait_ready(void)
{
    clock_hse_start();
    while (!LL_RCC_HSE_IsReady()) {
        if ((s_ms - s_hse_t0) > BOARD_HSE_TIMEOUT_MS) {
            LL_RCC_HSE_Disable();
            return false;
        }
    }
    return true;
}

uint32_t clock_fdcan_init(void)
{
    /* FDCAN chạy từ HSE. HSE không lên thì dùng PCLK1 (HSI ±1%, chỉ tin cậy ở bitrate thấp). */
    if (hse_wait_ready()) {
        LL_RCC_SetFDCANClockSource(LL_RCC_FDCAN_CLKSOURCE_HSE);
        s_fdcan_hz = BOARD_HSE_HZ;
    } else {
        LL_RCC_SetFDCANClockSource(LL_RCC_FDCAN_CLKSOURCE_PCLK1);
        s_fdcan_hz = BL_SYSCLK_HZ;
    }
    return s_fdcan_hz;
}

void clock_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    /* Range 1 normal mode cho phép tới 150 MHz với 4 wait state. */
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_4) {
    }
    FLASH->ACR |= FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* PLL: HSI16 / 4 * 75 / 2 = 150 MHz */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_4, 75, LL_RCC_PLLR_DIV_2);
    LL_RCC_PLL_Enable();
    LL_RCC_PLL_EnableDomain_SYS();
    while (!LL_RCC_PLL_IsReady()) {
    }

    /* Lên > 80 MHz phải qua bước trung gian AHB/2 ít nhất 1 µs (RM0440). */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_2);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
    }
    delay_cycles(200u);
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    SystemCoreClock = BL_SYSCLK_HZ;

    SysTick_Config(BL_SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 2u);
}

void clock_deinit(void)
{
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    NVIC_SetPriority(SysTick_IRQn, 0u);

    LL_RCC_SetFDCANClockSource(LL_RCC_FDCAN_CLKSOURCE_HSE); /* giá trị reset */
    LL_RCC_HSE_Disable();
    for (uint32_t t = 0; t < 100000u && LL_RCC_HSE_IsReady(); t++) {
    }
    LL_RCC_HSE_DisableBypass(); /* chỉ ghi được khi HSE đã tắt */

    /* Thứ tự: SYSCLK về HSI16 trước, rồi mới hạ latency và tắt PLL. */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
    }
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    SystemCoreClock = BL_HSI_HZ;

    /* Latency 0 WS, tắt prefetch. ICEN/DCEN (và DBG_SWEN) giữ nguyên: reset vốn bật. */
    FLASH->ACR &= ~(FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_0) {
    }

    LL_RCC_PLL_DisableDomain_SYS();
    LL_RCC_PLL_Disable();
    while (LL_RCC_PLL_IsReady()) {
    }
    RCC->PLLCFGR = 16UL << RCC_PLLCFGR_PLLN_Pos; /* giá trị reset 0x0000_1000 */

    /* Voltage scaling để nguyên range 1 (giá trị reset). */
    LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_PWR);

    s_ms = 0u;
    s_fdcan_hz = 0u;
    s_hse_started = false;
}
