/**
 * Bootloader PDS cho STM32G4 (xem bootloader/design.md).
 *
 * Luồng khởi động:
 *   1. Đọc magic trong backup register (app yêu cầu ở lại bootloader).
 *   2. Không có magic + app hợp lệ (header, CRC, valid flag) -> nhảy sang app
 *      ngay, khi clock còn ở trạng thái reset (HSI16), chưa bật ngoại vi nào.
 *   3. Ngược lại: lên 150 MHz, đọc config (NodeID, bitrate), chạy CANopen.
 */
#include <stdbool.h>

#include "boot_jump.h"
#include "canopen.h"
#include "clock.h"
#include "fdcan.h"
#include "image.h"
#include "pds/pds_boot.h"
#include "pds/pds_config.h"
#include "pds/pds_flash.h"
#include "pds/pds_layout.h"
#include "pds/pds_secver.h"
#include "program.h"
#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"

void NMI_Handler(void)
{
    if (!pds_flash_nmi_handler()) {
        NVIC_SystemReset(); /* NMI khác (CSS, SRAM parity...) */
    }
}

/* Đưa ngoại vi mà boot path đã chạm về trạng thái reset trước khi nhảy. */
static void boot_path_cleanup(void)
{
    LL_AHB1_GRP1_ForceReset(LL_AHB1_GRP1_PERIPH_CRC);
    LL_AHB1_GRP1_ReleaseReset(LL_AHB1_GRP1_PERIPH_CRC);
    LL_AHB1_GRP1_DisableClock(LL_AHB1_GRP1_PERIPH_CRC);
}

int main(void)
{
    bool stay = pds_boot_take_request();
    bool layout_ok = pds_layout_check();
    uint32_t sec_min = pds_secver_min();
    bool app_valid = layout_ok && image_check_boot(sec_min) == IMG_OK;

    if (!stay && app_valid) {
        boot_path_cleanup();
        boot_jump_to_app();
        /* Chỉ quay lại nếu vector table hỏng (đã kiểm tra ở trên). */
        app_valid = false;
    }

    clock_init();

    uint8_t node_id = PDS_CFG_DEFAULT_NODE_ID;
    uint8_t bitrate_idx = PDS_CFG_DEFAULT_BITRATE_IDX;
    if (layout_ok) {
        (void)pds_config_init();
        node_id = pds_config_node_id();
        bitrate_idx = pds_config_bitrate_idx();
    }

    program_init(layout_ok, app_valid, sec_min);

    if (!fdcan_init(clock_fdcan_hz(), pds_bitrate_from_idx(bitrate_idx), node_id)) {
        /* Bitrate không chia được từ clock hiện có: thử mặc định. */
        (void)fdcan_init(clock_fdcan_hz(), pds_bitrate_from_idx(PDS_CFG_DEFAULT_BITRATE_IDX), node_id);
    }
    co_init(node_id);

    for (;;) {
        watchdog_kick();
        co_process();
    }
}
