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
 *   - Macro bỏ trống thì lấy mặc định trong src/board.h (và các header của
 *     common/ cho phần dùng chung).
 *   - File được -include vào mọi file nguồn. Build app của board này với cùng
 *     file (BLC_BOARD_FILE trong blc_common.mk / CMake) để phần dùng chung khớp.
 */

/* Chip trên board. Bỏ dòng này nếu board lắp được nhiều chip, khi đó chọn
 * bằng make MCU=... */
#define BOARD_MCU               STM32G474xx

/* Clock FDCAN từ HSE. 0 = không có HSE (dùng PCLK1, chỉ hợp bitrate thấp). */
#define BOARD_HSE_HZ            24000000u
#define BOARD_HSE_BYPASS        0       /* 1 = nguồn clock ngoài (oscillator) */
#define BOARD_HSE_TIMEOUT_MS    10u     /* chờ HSE tối đa, chạy ở mọi lần boot */

/* FDCAN2 trên PB12 (RX) / PB13 (TX). AF mặc định: 9 cho FDCAN1/2, 11 cho FDCAN3. */
#define BOARD_CAN_INSTANCE      2
#define BOARD_CAN_RX            B, 12
#define BOARD_CAN_TX            B, 13
/* #define BOARD_CAN_AF         9 */

/* Chân cấp nguồn cho transceiver (tùy chọn): PC13, mức 1 = có nguồn, chờ
 * 5 ms cho nguồn ổn định trước khi cấu hình chân CAN. Cửa sổ cứu node chạy ở
 * mọi lần boot, nên thời gian chờ này cộng vào thời gian boot. */
#define BOARD_CAN_PWR           C, 13
#define BOARD_CAN_PWR_ACTIVE    1
#define BOARD_CAN_PWR_DELAY_MS  5u

/* Chân STBY/EN của transceiver (tùy chọn): PB14, mức 0 = hoạt động. */
#define BOARD_CAN_STBY          B, 14
#define BOARD_CAN_STBY_ACTIVE   0

/* ---- Dùng chung app + bootloader (common/) ----
 * Mặc định khi config chưa có giá trị (chip mới). NodeID 1..127, bitrate
 * theo bảng CiA 305: BLC_BITRATE_IDX_1000K/800K/500K/250K/125K/50K/20K/10K. */
#define BLC_CFG_DEFAULT_NODE_ID      10u
#define BLC_CFG_DEFAULT_BITRATE_IDX  BLC_BITRATE_IDX_500K

/* Identity CANopen 0x1000 / 0x1018 (mặc định 0). Vendor ID do CiA cấp. */
/* #define BLC_ID_DEVICE_TYPE    0x00000000UL */
/* #define BLC_ID_VENDOR_ID      0x00000000UL */
/* #define BLC_ID_PRODUCT_CODE   0x00000000UL */
/* #define BLC_ID_REVISION       0x00000000UL */

/* Cửa sổ cứu node lúc boot (ms, 0 = tắt), backup register cho handoff. */
/* #define BL_RESCUE_WINDOW_MIN_MS  20u */
/* #define BLC_BOOT_BKP_INDEX       0u  */
