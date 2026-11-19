/**
 * @file blc_config_ids.h
 * @brief Danh sách ID cấu hình và giá trị mặc định (design.md mục 6.4).
 *
 * Dùng chung cho app và bootloader để hai bên luôn giống nhau.
 * Tag hợp lệ: 0x01..0xFE (0x00 = ô đã xóa, 0xFF = ô trống).
 *
 * Quy ước dải ID:
 *   0x01..0x0F  cấu hình dùng chung app + bootloader
 *   0x10..0xFE  cấu hình riêng của app
 */
#ifndef BLC_CONFIG_IDS_H
#define BLC_CONFIG_IDS_H

/* ---- ID ----------------------------------------------------------------- */
#define BLC_CFG_ID_NODE_ID        0x01u   /* data[0] = NodeID (1..127) */
#define BLC_CFG_ID_CAN_BITRATE    0x02u   /* data[0] = chỉ số bảng bit timing CiA 305 */

#define BLC_CFG_ID_APP_FIRST      0x10u   /* ID đầu tiên dành cho app */

/* ---- Bảng bitrate theo CiA 305 (LSS) ------------------------------------ */
#define BLC_BITRATE_IDX_1000K     0u
#define BLC_BITRATE_IDX_800K      1u
#define BLC_BITRATE_IDX_500K      2u
#define BLC_BITRATE_IDX_250K      3u
#define BLC_BITRATE_IDX_125K      4u
/* 5 = reserved */
#define BLC_BITRATE_IDX_50K       6u
#define BLC_BITRATE_IDX_20K       7u
#define BLC_BITRATE_IDX_10K       8u

/* ---- Giá trị mặc định ---------------------------------------------------
 * Dùng khi config chưa có giá trị (chip mới) hoặc giá trị hỏng. Không sửa ở
 * đây cho từng board: đặt trong file board (BLC_BOARD_FILE, xem blc_common.mk)
 * hoặc -D. App và bootloader phải dùng cùng giá trị, nếu không chip mới sẽ
 * chạy NodeID/bitrate khác nhau ở hai bên.
 * TODO(design.md mục 7): giá trị mặc định chính thức chưa chốt.
 */
#ifndef BLC_CFG_DEFAULT_NODE_ID
#define BLC_CFG_DEFAULT_NODE_ID       127u
#endif
#ifndef BLC_CFG_DEFAULT_BITRATE_IDX
#define BLC_CFG_DEFAULT_BITRATE_IDX   BLC_BITRATE_IDX_250K
#endif

#if BLC_CFG_DEFAULT_NODE_ID < 1 || BLC_CFG_DEFAULT_NODE_ID > 127
#error "BLC_CFG_DEFAULT_NODE_ID phải trong 1..127"
#endif
#if BLC_CFG_DEFAULT_BITRATE_IDX > BLC_BITRATE_IDX_10K || BLC_CFG_DEFAULT_BITRATE_IDX == 5
#error "BLC_CFG_DEFAULT_BITRATE_IDX không có trong bảng CiA 305 (dùng BLC_BITRATE_IDX_*)"
#endif

#endif /* BLC_CONFIG_IDS_H */
