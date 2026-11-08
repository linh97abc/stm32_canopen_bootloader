/**
 * Ví dụ tích hợp thư viện common/ vào app (dùng được với dự án HAL/CubeMX).
 *
 * App cần:
 *   1. Khai báo header image (PDS_APP_HEADER_DEFINE) đúng 1 lần.
 *   2. Đặt VTOR = PDS_APP_START (trong SystemInit: USER_VECT_TAB_ADDRESS,
 *      VECT_TAB_OFFSET = PDS_APP_START - FLASH_BASE). Bootloader cũng đã đặt
 *      VTOR trước khi nhảy, nhưng SystemInit của Cube thường ghi đè lại.
 *   3. Gọi pds_flash_nmi_handler() trong NMI_Handler.
 *   4. Đọc NodeID/bitrate từ config (giống hệt bootloader).
 *   5. Khi nhận 0x1F51:01 = 0 (Stop program): gửi SDO response, rồi
 *      pds_boot_enter_bootloader().
 *   6. Identity 0x1000/0x1018 lấy từ pds_identity.h.
 */
#include "pds/pds_app_header.h"
#include "pds/pds_boot.h"
#include "pds/pds_config.h"
#include "pds/pds_flash.h"
#include "pds/pds_identity.h"
#include "pds/pds_layout.h"

/* (1) app_version 1.0.0, security_version 0. Chỉ tăng security_version cho bản vá bảo mật. */
PDS_APP_HEADER_DEFINE(PDS_APP_VERSION(1, 0, 0), 0);

/* (3) Nếu dùng file stm32g4xx_it.c của Cube, sửa NMI_Handler ở đó thay vì ở đây. */
void NMI_Handler(void)
{
    if (pds_flash_nmi_handler()) {
        return; /* lỗi ECC khi đọc flash: pds_config/pds_secver đã tự xử lý */
    }
    for (;;) {
        /* NMI khác: CSS, SRAM parity... */
    }
}

/* (4) Gọi sớm trong main(), trước khi khởi tạo stack CANopen. */
void app_load_can_settings(uint8_t *node_id, uint32_t *bitrate)
{
    if (pds_layout_check()) {
        (void)pds_config_init();
    }
    *node_id = pds_config_node_id();
    *bitrate = pds_bitrate_from_idx(pds_config_bitrate_idx());
}

/* LSS (nếu app hỗ trợ) lưu cấu hình qua thư viện; bootloader dùng ở lần boot sau. */
void app_on_lss_store_configuration(uint8_t node_id, uint8_t cia305_bitrate_idx)
{
    (void)pds_config_set_node_id(node_id);
    (void)pds_config_set_bitrate_idx(cia305_bitrate_idx);
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
    return 0x08000022u;            /* app không xử lý Start/Clear */
}

/* Gọi trong main loop, sau khi stack CANopen đã xử lý/gửi xong frame. */
void app_poll_bootloader_request(void)
{
    if (s_enter_bl_pending) {
        /* Chờ TX FIFO CAN rỗng (tùy stack) rồi: */
        pds_boot_enter_bootloader();
    }
}
