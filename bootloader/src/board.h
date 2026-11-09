/**
 * @file board.h
 * @brief Cấu hình phần cứng của board cho bootloader.
 *
 * TODO: chỉnh theo schematic thật. Mọi macro đều ghi đè được bằng -D.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32g4xx.h"
#include "stm32g4xx_ll_gpio.h"

/* Thạch anh ngoài, dùng làm clock cho FDCAN (CAN cần sai số < ~0.5%). */
#ifndef BOARD_HSE_HZ
#define BOARD_HSE_HZ            8000000u
#endif
#ifndef BOARD_HSE_BYPASS
#define BOARD_HSE_BYPASS        0       /* 1 = nguồn clock ngoài thay cho thạch anh */
#endif
/* Chờ HSE tối đa (ms). Chạy ở mọi lần boot (cửa sổ cứu node), nên giữ ngắn. */
#ifndef BOARD_HSE_TIMEOUT_MS
#define BOARD_HSE_TIMEOUT_MS    10u
#endif

/* FDCAN1: mặc định PA11 = RX, PA12 = TX (AF9). */
#ifndef BOARD_CAN_GPIO
#define BOARD_CAN_GPIO          GPIOA
#define BOARD_CAN_GPIO_CLK      RCC_AHB2ENR_GPIOAEN
#define BOARD_CAN_RX_PIN        LL_GPIO_PIN_11
#define BOARD_CAN_TX_PIN        LL_GPIO_PIN_12
#define BOARD_CAN_AF            LL_GPIO_AF_9
#endif

/* Chân STBY/EN của transceiver (tùy chọn). Bỏ trống nếu không có. */
/* #define BOARD_CAN_STBY_GPIO     GPIOB                */
/* #define BOARD_CAN_STBY_GPIO_CLK RCC_AHB2ENR_GPIOBEN  */
/* #define BOARD_CAN_STBY_PIN      LL_GPIO_PIN_5        */
/* #define BOARD_CAN_STBY_ACTIVE   0  (mức logic để transceiver hoạt động) */

#endif /* BOARD_H */
