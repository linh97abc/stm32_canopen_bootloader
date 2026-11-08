#ifndef BL_CLOCK_H
#define BL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define BL_SYSCLK_HZ  150000000u

/** SYSCLK = 150 MHz (PLL từ HSI16), SysTick 1 ms, FDCAN clock từ HSE. */
void clock_init(void);

/** Tần số kernel clock của FDCAN (HSE, hoặc PCLK1 nếu HSE không lên). */
uint32_t clock_fdcan_hz(void);

/** Bộ đếm ms từ SysTick. */
uint32_t clock_ms(void);

/** Nạp lại IWDG (vô hại nếu IWDG chưa được bật). */
void watchdog_kick(void);

#endif
