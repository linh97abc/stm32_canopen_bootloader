/**
 * Ví dụ tích hợp thư viện common/ vào app (dùng được với dự án HAL/CubeMX).
 *
 * App cần:
 *   1. Linker script chừa .app_header (xem stm32g4_app.ld.S); version điền
 *      lúc ký: blc_image.py sign --app-version x.y.z --sec-version N.
 *   2. Đặt VTOR = BLC_APP_START (trong SystemInit: USER_VECT_TAB_ADDRESS,
 *      VECT_TAB_OFFSET = BLC_APP_START - FLASH_BASE). Bootloader cũng đã đặt
 *      VTOR trước khi nhảy, nhưng SystemInit của Cube thường ghi đè lại.
 *   3. Gọi blc_flash_nmi_handler() trong NMI_Handler.
 *   4. Đọc NodeID/bitrate từ config (giống hệt bootloader).
 *   5. Khi nhận 0x1F51:01 = 0 (Stop program): gửi SDO response, rồi
 *      blc_boot_enter_bootloader(). Ghi 0x1F51:01 = 1 (Start) trả OK, đọc
 *      0x1F51:01 trả 1 (đang chạy) để master phân biệt app với bootloader.
 *   6. Identity 0x1000/0x1018 lấy từ blc_identity.h.
 */
#include "blc/blc_boot.h"
#include "blc/blc_config.h"
#include "blc/blc_flash.h"
#include "blc/blc_identity.h"
#include "blc/blc_layout.h"

/* (3) Nếu dùng file stm32g4xx_it.c của Cube, sửa NMI_Handler ở đó thay vì ở đây. */
void NMI_Handler(void)
{
    if (blc_flash_nmi_handler()) {
        return; /* lỗi ECC khi đọc flash: blc_config/blc_secver đã tự xử lý */
    }
    for (;;) {
        /* NMI khác: CSS, SRAM parity... */
    }
}

/* (4) Gọi sớm trong main(), trước khi khởi tạo stack CANopen. */
void app_load_can_settings(uint8_t *node_id, uint32_t *bitrate)
{
    if (blc_layout_check()) {
        (void)blc_config_init();
    }
    *node_id = blc_config_node_id();
    *bitrate = blc_bitrate_from_idx(blc_config_bitrate_idx());
}

/* LSS (nếu app hỗ trợ) lưu cấu hình qua thư viện; bootloader dùng ở lần boot sau. */
void app_on_lss_store_configuration(uint8_t node_id, uint8_t cia305_bitrate_idx)
{
    (void)blc_config_set_node_id(node_id);
    (void)blc_config_set_bitrate_idx(cia305_bitrate_idx);
}

/* (5) Callback ghi 0x1F51:01 trong OD của stack CANopen app (vd. CANopenNode).
 * Trả về 0 nếu OK, hoặc mã SDO abort. */
static volatile int s_enter_bl_pending;

uint32_t app_od_write_1F51(uint8_t value)
{
    if (value == 0u) {             /* Stop program */
        s_enter_bl_pending = 1;    /* reset SAU KHI stack đã gửi SDO response */
        return 0u;
    }
    if (value == 1u) {
        /* Start program: app đang chạy rồi -> OK. Master gửi lại Start khi mất
         * response của bootloader (bootloader đã reset sang app) sẽ không bị
         * báo nạp lỗi. */
        return 0u;
    }
    return 0x08000022u;            /* Clear/Reset: phải Stop về bootloader trước */
}

/* Đọc 0x1F51:01: 1 = program started. Bootloader trả 0 (có app) hoặc 3 (không có app). */
uint8_t app_od_read_1F51(void)
{
    return 1u;
}

/* Gọi trong main loop, sau khi stack CANopen đã xử lý/gửi xong frame. */
void app_poll_bootloader_request(void)
{
    if (s_enter_bl_pending) {
        /* Chờ TX FIFO CAN rỗng (tùy stack) rồi: */
        blc_boot_enter_bootloader();
    }
}
