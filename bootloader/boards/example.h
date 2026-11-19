/**
 * Mẫu file board, liệt kê mọi tùy chọn. Đây KHÔNG phải board thật: chép ra
 * thành <tên board>.h rồi sửa theo schematic.
 *
 *   make -C bootloader BOARD=<tên board>                  (file trong boards/)
 *   make -C bootloader BOARD_FILE=/đường/dẫn/myboard.h    (file ngoài repo)
 *
 * Quy tắc:
 *   - Chỉ dùng #define, không #include (Makefile đọc BOARD_MCU bằng gcc -E).
 *   - Chân GPIO viết dạng "cổng, số": B, 12 nghĩa là PB12.
 *   - Macro bỏ trống thì lấy mặc định trong src/board.h.
 */

/* Chip trên board. Bỏ dòng này nếu board lắp được nhiều chip, khi đó chọn
 * bằng make MCU=... */
#define BOARD_MCU               STM32G474xx

/* Clock FDCAN từ HSE. 0 = không có HSE (dùng PCLK1, chỉ hợp bitrate thấp). */
#define BOARD_HSE_HZ            24000000u
#define BOARD_HSE_BYPASS        0       /* 1 = nguồn clock ngoài (oscillator) */

/* FDCAN2 trên PB12 (RX) / PB13 (TX). AF mặc định: 9 cho FDCAN1/2, 11 cho FDCAN3. */
#define BOARD_CAN_INSTANCE      2
#define BOARD_CAN_RX            B, 12
#define BOARD_CAN_TX            B, 13
/* #define BOARD_CAN_AF         9 */

/* Chân cấp nguồn cho transceiver (tùy chọn): PC13, mức 1 = có nguồn, chờ
 * 5 ms cho nguồn ổn định trước khi cấu hình chân CAN. */
#define BOARD_CAN_PWR           C, 13
#define BOARD_CAN_PWR_ACTIVE    1
#define BOARD_CAN_PWR_DELAY_MS  5u

/* Chân STBY/EN của transceiver (tùy chọn): PB14, mức 0 = hoạt động. */
#define BOARD_CAN_STBY          B, 14
#define BOARD_CAN_STBY_ACTIVE   0
