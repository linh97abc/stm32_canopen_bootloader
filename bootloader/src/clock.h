#ifndef BL_CLOCK_H
#define BL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define BL_SYSCLK_HZ  150000000u
#define BL_HSI_HZ     16000000u

/** SYSCLK = 150 MHz (PLL từ HSI16), SysTick 1 ms. Chưa bật HSE. */
void clock_init(void);

/**
 * Bật HSE, không chờ ổn định: gọi trước việc khác (kiểm tra app) để thời gian
 * HSE lên trùng với việc đó. Gọi sau clock_init(). Gọi lại thì không làm gì.
 */
void clock_hse_start(void);

/**
 * Chờ HSE (tự bật nếu chưa gọi clock_hse_start) rồi chọn làm clock FDCAN;
 * HSE không lên trong BOARD_HSE_TIMEOUT_MS tính từ lúc bật thì dùng PCLK1.
 * Gọi sau clock_init(). Trả về kernel clock.
 */
uint32_t clock_fdcan_init(void);

/**
 * Trả mọi thứ clock_init()/clock_fdcan_init() đã đổi về trạng thái reset
 * trước khi nhảy sang app: SysTick, HSE, nguồn clock FDCAN, SYSCLK = HSI16,
 * flash latency, PLL, clock PWR/SYSCFG (docs/design.md mục 3.1).
 */
void clock_deinit(void);

/** Bộ đếm ms từ SysTick. */
uint32_t clock_ms(void);

/** Nạp lại IWDG (vô hại nếu IWDG chưa được bật). */
void watchdog_kick(void);

#endif
