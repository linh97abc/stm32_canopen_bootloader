/**
 * @file blc_boot.h
 * @brief Yêu cầu cho lần boot kế tiếp qua backup register (design.md mục 3).
 *
 * App: khi nhận Stop program (0x1F51:01 = 0), gửi xong SDO response rồi gọi
 *      blc_boot_enter_bootloader().
 * Bootloader: Start program thành công, gửi xong SDO response rồi gọi
 *      blc_boot_start_app(). Lúc khởi động gọi blc_boot_take_request().
 */
#ifndef BLC_BOOT_H
#define BLC_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chỉ số backup register TAMP_BKPxR dùng cho magic. App không được dùng thanh ghi này. */
#ifndef BLC_BOOT_BKP_INDEX
#define BLC_BOOT_BKP_INDEX     0u
#endif

#define BLC_BOOT_MAGIC_STAY_IN_BL  0x51524C42UL   /* "BLRQ": ở lại bootloader */
#define BLC_BOOT_MAGIC_START_APP   0x4F474C42UL   /* "BLGO": vào app, bỏ qua cửa sổ cứu node */

typedef enum {
    BLC_BOOT_REQ_NONE,       /* không có magic: boot bình thường (có cửa sổ cứu node) */
    BLC_BOOT_REQ_STAY,       /* app yêu cầu ở lại bootloader (Stop) */
    BLC_BOOT_REQ_START_APP,  /* bootloader vừa nhận Start: vào app nếu hợp lệ, không mở cửa sổ */
} blc_boot_req_t;

/** App: ghi magic STAY_IN_BL rồi reset. Không trả về. */
__attribute__((noreturn)) void blc_boot_enter_bootloader(void);

/** Bootloader: ghi magic START_APP rồi reset. Không trả về. */
__attribute__((noreturn)) void blc_boot_start_app(void);

/** Bootloader: đọc yêu cầu trong backup register, xóa magic nếu có. */
blc_boot_req_t blc_boot_take_request(void);

#ifdef __cplusplus
}
#endif

#endif /* BLC_BOOT_H */
