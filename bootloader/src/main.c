/**
 * Bootloader CANopen cho STM32G4 (xem docs/design.md).
 *
 * Luồng khởi động:
 *   1. Lên 150 MHz ở mọi nhánh, đọc config (NodeID, bitrate).
 *   2. Đọc và xóa magic trong backup register (app yêu cầu ở lại bootloader),
 *      kiểm tra app.
 *   3. Không có magic + app hợp lệ (header, CRC, valid flag) -> mở cửa sổ cứu
 *      node (mục 3.1): bật FDCAN, chỉ nghe T ms. Không bắt được Stop thì đưa
 *      mọi thứ đã dùng (kể cả clock) về trạng thái reset rồi nhảy sang app.
 *   4. Ngược lại: chạy CANopen. Nếu đến từ cửa sổ thì FDCAN đã chạy sẵn, xử lý
 *      tiếp lệnh Stop đã bắt được (trả OK).
 */
#include <stdbool.h>

#include "boot_jump.h"
#include "canopen.h"
#include "clock.h"
#include "fdcan.h"
#include "image.h"
#include "blc/blc_boot.h"
#include "blc/blc_config.h"
#include "blc/blc_flash.h"
#include "blc/blc_layout.h"
#include "blc/blc_secver.h"
#include "program.h"
#include "rescue.h"
#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"

void NMI_Handler(void)
{
    if (!blc_flash_nmi_handler()) {
        NVIC_SystemReset(); /* NMI khác (CSS, SRAM parity...) */
    }
}

/*
 * Đưa mọi thứ boot path đã chạm về trạng thái reset trước khi nhảy
 * (docs/design.md mục 3.1, bảng dọn dẹp). FDCAN và GPIO của CAN đã được
 * fdcan_deinit() dọn khi đóng cửa sổ; NVIC do boot_jump_to_app() dọn. Flash
 * tự khóa sau mỗi lần ghi, backup domain đóng ngay sau khi đọc magic.
 */
static void boot_path_cleanup(void)
{
    LL_AHB1_GRP1_ForceReset(LL_AHB1_GRP1_PERIPH_CRC);
    LL_AHB1_GRP1_ReleaseReset(LL_AHB1_GRP1_PERIPH_CRC);
    LL_AHB1_GRP1_DisableClock(LL_AHB1_GRP1_PERIPH_CRC);
    clock_deinit(); /* cuối cùng: SYSCLK về HSI16 */
}

/* Bật HSE + FDCAN, dùng chung cho cửa sổ và nhánh ở lại. Trả về bitrate đã dùng, 0 nếu lỗi. */
static uint32_t can_start(uint8_t node_id, uint8_t bitrate_idx)
{
    uint32_t hz = clock_fdcan_init();
    uint32_t bitrate = blc_bitrate_from_idx(bitrate_idx);
    if (fdcan_init(hz, bitrate, node_id)) {
        return bitrate;
    }
    /* Bitrate không chia được từ clock hiện có: thử mặc định. */
    bitrate = blc_bitrate_from_idx(BLC_CFG_DEFAULT_BITRATE_IDX);
    return fdcan_init(hz, bitrate, node_id) ? bitrate : 0u;
}

/*
 * Cửa sổ cứu node (mục 3.1). Bắt được Stop: trả true, *stop = frame đó và
 * FDCAN vẫn chạy cho nhánh ở lại. Không bắt được: tắt FDCAN, trả false.
 */
static bool rescue_window(uint8_t node_id, uint8_t bitrate_idx, can_frame_t *stop)
{
    if (BL_RESCUE_WINDOW_MIN_MS == 0u) {
        return false;
    }
    uint32_t bitrate = can_start(node_id, bitrate_idx);
    /* T tính từ lúc FDCAN đã thoát init. */
    if (bitrate != 0u && rescue_wait(node_id, rescue_window_ms(bitrate), stop)) {
        return true;
    }
    fdcan_deinit();
    return false;
}

int main(void)
{
    clock_init();

    bool layout_ok = blc_layout_check();
    uint8_t node_id = BLC_CFG_DEFAULT_NODE_ID;
    uint8_t bitrate_idx = BLC_CFG_DEFAULT_BITRATE_IDX;
    if (layout_ok) {
        (void)blc_config_init();
        node_id = blc_config_node_id();
        bitrate_idx = blc_config_bitrate_idx();
    }

    /* Đọc và xóa magic sau config: reset giữa chừng thì yêu cầu ở lại vẫn còn. */
    bool stay = blc_boot_take_request();
    uint32_t sec_min = blc_secver_min();
    bool app_valid = layout_ok && image_check_boot(sec_min) == IMG_OK;

    can_frame_t stop;
    bool caught = false;
    if (!stay && app_valid) {
        caught = rescue_window(node_id, bitrate_idx, &stop);
        if (!caught) {
            boot_path_cleanup();
            boot_jump_to_app();
            /* Chỉ quay lại nếu vector table hỏng (đã kiểm tra ở trên). */
            app_valid = false;
            clock_init();
        }
    }

    program_init(layout_ok, app_valid, sec_min);

    if (!caught) {
        (void)can_start(node_id, bitrate_idx);
    }
    co_init(node_id); /* gửi boot-up */
    if (caught) {
        co_handle(&stop); /* đã ở bootloader: Stop chỉ trả OK */
    }

    for (;;) {
        watchdog_kick();
        co_process();
    }
}
