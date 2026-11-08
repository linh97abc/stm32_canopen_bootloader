/**
 * @file pds_config_ids.h
 * @brief Danh sách ID cấu hình và giá trị mặc định (design.md mục 6.4).
 *
 * Dùng chung cho app và bootloader để hai bên luôn giống nhau.
 * Tag hợp lệ: 0x01..0xFE (0x00 = ô đã xóa, 0xFF = ô trống).
 *
 * Quy ước dải ID:
 *   0x01..0x0F  cấu hình dùng chung app + bootloader
 *   0x10..0xFE  cấu hình riêng của app
 */
#ifndef PDS_CONFIG_IDS_H
#define PDS_CONFIG_IDS_H

/* ---- ID ----------------------------------------------------------------- */
#define PDS_CFG_ID_NODE_ID        0x01u   /* data[0] = NodeID (1..127) */
#define PDS_CFG_ID_CAN_BITRATE    0x02u   /* data[0] = chỉ số bảng bit timing CiA 305 */

#define PDS_CFG_ID_APP_FIRST      0x10u   /* ID đầu tiên dành cho app */

/* ---- Bảng bitrate theo CiA 305 (LSS) ------------------------------------ */
#define PDS_BITRATE_IDX_1000K     0u
#define PDS_BITRATE_IDX_800K      1u
#define PDS_BITRATE_IDX_500K      2u
#define PDS_BITRATE_IDX_250K      3u
#define PDS_BITRATE_IDX_125K      4u
/* 5 = reserved */
#define PDS_BITRATE_IDX_50K       6u
#define PDS_BITRATE_IDX_20K       7u
#define PDS_BITRATE_IDX_10K       8u

/* ---- Giá trị mặc định ---------------------------------------------------
 * TODO(design.md mục 7): giá trị chính thức chưa chốt. Có thể ghi đè bằng -D.
 */
#ifndef PDS_CFG_DEFAULT_NODE_ID
#define PDS_CFG_DEFAULT_NODE_ID       127u
#endif
#ifndef PDS_CFG_DEFAULT_BITRATE_IDX
#define PDS_CFG_DEFAULT_BITRATE_IDX   PDS_BITRATE_IDX_250K
#endif

#endif /* PDS_CONFIG_IDS_H */
