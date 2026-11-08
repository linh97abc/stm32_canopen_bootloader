/**
 * @file blc_config.h
 * @brief Lưu cấu hình dạng log append trong 1 page flash (design.md mục 6).
 *
 * Bản ghi 8 byte = 1 double-word: [tag][data 6 byte][CRC-8 của byte 0..6].
 * - Ghi: append vào ô trống sau ô đã dùng cuối cùng (chỉ ghi khi giá trị đổi).
 * - Đọc: bản ghi hợp lệ cuối cùng của mỗi tag (được cache trong RAM lúc init).
 * - Page đầy: gom bản ghi mới nhất của từng tag, xóa page, ghi lại.
 * - Lỗi ECC: 1 ô lỗi ngay cuối log (mất điện lúc append) -> ghi 0x00 để vô hiệu;
 *   lỗi ở chỗ khác (mất điện lúc xóa page) -> xóa page, dùng mặc định.
 *
 * Không thread-safe: chỉ gọi từ một ngữ cảnh (main loop / 1 task).
 */
#ifndef BLC_CONFIG_H
#define BLC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "blc/blc_config_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLC_CFG_DATA_LEN   6u

/** Số tag tối đa được cache (RAM = 7 byte/tag). */
#ifndef BLC_CFG_MAX_IDS
#define BLC_CFG_MAX_IDS    32u
#endif

typedef enum {
    BLC_CFG_OK = 0,
    BLC_CFG_REPAIRED,      /* init: đã vô hiệu 1 bản ghi ghi dở */
    BLC_CFG_PAGE_RESET,    /* init: page hỏng, đã xóa, mọi cấu hình về mặc định */
    BLC_CFG_ERR_PARAM,
    BLC_CFG_ERR_FLASH,
    BLC_CFG_ERR_NO_SPACE,  /* hết chỗ cache (quá BLC_CFG_MAX_IDS tag) */
    BLC_CFG_ERR_NOT_INIT,
} blc_cfg_status_t;

/**
 * Quét page config, dựng cache, sửa lỗi nếu cần. Gọi 1 lần lúc khởi động,
 * sau khi đã bật NMI handler cho lỗi ECC (blc_flash_nmi_handler).
 * Chỉ gọi khi blc_layout_check() == true.
 */
blc_cfg_status_t blc_config_init(void);

/** Đọc dữ liệu thô của tag. false nếu không có bản ghi hợp lệ. */
bool blc_config_read(uint8_t id, uint8_t out[BLC_CFG_DATA_LEN]);

/** Ghi dữ liệu thô (len <= 6, phần còn lại đệm 0xFF). Không ghi nếu không đổi. */
blc_cfg_status_t blc_config_write(uint8_t id, const void *data, uint8_t len);

/** Xóa toàn bộ config (về mặc định). */
blc_cfg_status_t blc_config_erase_all(void);

/* ---- Tiện ích kiểu số (little-endian, data[0..n-1]) ---------------------- */
uint32_t blc_config_get_u32(uint8_t id, uint32_t def);
blc_cfg_status_t blc_config_set_u32(uint8_t id, uint32_t value);

/* ---- Cấu hình dùng chung app + bootloader (đã kiểm tra miền giá trị) ---- */

/** NodeID hợp lệ (1..127), hoặc mặc định nếu không có/hỏng. */
uint8_t blc_config_node_id(void);
blc_cfg_status_t blc_config_set_node_id(uint8_t node_id);

/** Chỉ số bitrate CiA 305 hợp lệ, hoặc mặc định nếu không có/hỏng. */
uint8_t blc_config_bitrate_idx(void);
blc_cfg_status_t blc_config_set_bitrate_idx(uint8_t idx);

/** Đổi chỉ số CiA 305 sang bit/s, 0 nếu không hỗ trợ. */
uint32_t blc_bitrate_from_idx(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* BLC_CONFIG_H */
