/**
 * @file board.h
 * @brief Cấu hình phần cứng theo board: nạp file board, điền giá trị mặc định.
 *
 * Không sửa file này để thêm board. File board chọn lúc build:
 *   make BOARD=<tên>               -> bootloader/boards/<tên>.h
 *   make BOARD_FILE=<đường dẫn>.h  -> file bất kỳ, kể cả ngoài repo
 *
 * File board chỉ chứa #define (không #include gì), xem boards/example.h. Nó
 * được -include vào mọi file nguồn (BLC_BOARD_FILE trong blc_common.mk), nên
 * đặt được cả macro của common/ (NodeID/bitrate mặc định, identity...).
 * Macro nào file board không định nghĩa thì lấy mặc định bên dưới, và cũng
 * ghi đè được bằng EXTRA_CFLAGS="-D...".
 *
 * Chân GPIO viết dạng "cổng, số": #define BOARD_CAN_RX  B, 8   (= PB8)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#include "stm32g4xx.h"

/* ---- Clock ---- */

/* Tần số HSE, làm clock cho FDCAN (CAN cần sai số < ~0.5%).
 * 0 = board không có HSE: FDCAN chạy bằng PCLK1 (HSI ±1%, chỉ hợp bitrate thấp). */
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

/* ---- FDCAN ---- */

#ifndef BOARD_CAN_INSTANCE
#define BOARD_CAN_INSTANCE      1       /* 1, 2 hoặc 3 (FDCAN2/3 tùy dòng chip) */
#endif
#ifndef BOARD_CAN_RX
#define BOARD_CAN_RX            A, 11
#endif
#ifndef BOARD_CAN_TX
#define BOARD_CAN_TX            A, 12
#endif
#ifndef BOARD_CAN_AF
#if BOARD_CAN_INSTANCE == 3
#define BOARD_CAN_AF            11      /* FDCAN3: AF11 */
#else
#define BOARD_CAN_AF            9       /* FDCAN1/2: AF9 */
#endif
#endif

/* ---- Transceiver (tùy chọn) ----
 *
 * BOARD_CAN_PWR: chân bật nguồn cho transceiver. Bật trước, chờ
 *   BOARD_CAN_PWR_DELAY_MS cho nguồn ổn định, rồi mới cấu hình chân CAN
 *   (tránh cấp ngược nguồn qua chân TX khi transceiver chưa có nguồn).
 * BOARD_CAN_STBY: chân STBY/EN/S, đưa về mức hoạt động sau cùng.
 *
 * Bootloader bật các chân này mỗi khi chạy FDCAN, kể cả cửa sổ cứu node ở mọi
 * lần boot (boot chậm thêm BOARD_CAN_PWR_DELAY_MS). fdcan_deinit() reset port
 * trước khi nhảy sang app: transceiver mất nguồn, app tự bật lại.
 */
#ifdef BOARD_CAN_PWR
#ifndef BOARD_CAN_PWR_ACTIVE
#define BOARD_CAN_PWR_ACTIVE    1       /* mức logic để cấp nguồn */
#endif
#ifndef BOARD_CAN_PWR_DELAY_MS
#define BOARD_CAN_PWR_DELAY_MS  5u
#endif
#endif

#ifdef BOARD_CAN_STBY
#ifndef BOARD_CAN_STBY_ACTIVE
#define BOARD_CAN_STBY_ACTIVE   0       /* mức logic để transceiver hoạt động */
#endif
#endif

/* ---- Suy ra từ cấu hình trên, không cần sửa ---- */

#if BOARD_CAN_INSTANCE == 1
#define BOARD_CAN_FDCAN         FDCAN1
#define BOARD_CAN_IRQn          FDCAN1_IT0_IRQn
#define BOARD_CAN_IRQHandler    FDCAN1_IT0_IRQHandler
#elif BOARD_CAN_INSTANCE == 2
#if !defined(FDCAN2)
#error "Dòng chip này không có FDCAN2"
#endif
#define BOARD_CAN_FDCAN         FDCAN2
#define BOARD_CAN_IRQn          FDCAN2_IT0_IRQn
#define BOARD_CAN_IRQHandler    FDCAN2_IT0_IRQHandler
#elif BOARD_CAN_INSTANCE == 3
#if !defined(FDCAN3)
#error "Dòng chip này không có FDCAN3"
#endif
#define BOARD_CAN_FDCAN         FDCAN3
#define BOARD_CAN_IRQn          FDCAN3_IT0_IRQn
#define BOARD_CAN_IRQHandler    FDCAN3_IT0_IRQHandler
#else
#error "BOARD_CAN_INSTANCE phải là 1, 2 hoặc 3"
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint32_t rcc_en;    /* bit trong RCC->AHB2ENR */
    uint32_t num;       /* 0..15 */
} board_pin_t;

/* BOARD_PIN(BOARD_CAN_RX) -> {GPIOB, RCC_AHB2ENR_GPIOBEN, 8} */
#define BOARD_PIN(p)            BOARD_PIN_(p)
#define BOARD_PIN_(port, n)     {GPIO##port, RCC_AHB2ENR_GPIO##port##EN, (n)}
#define BOARD_PIN_NUM(p)        BOARD_PIN_NUM_(p)
#define BOARD_PIN_NUM_(port, n) (n)

#endif /* BOARD_H */
