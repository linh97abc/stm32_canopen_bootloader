#include "program.h"

#include <string.h>

#include "clock.h"
#include "image.h"
#include "blc/blc_app_header.h"
#include "blc/blc_flash.h"
#include "blc/blc_layout.h"
#include "blc/blc_secver.h"
#include "sdo_abort.h"

#define SIG_MAX 256u
#define HDR_END (BLC_APP_HEADER_OFFSET + sizeof(blc_app_header_t))

typedef enum {
    ST_UNKNOWN,     /* chưa Clear: flash app chưa sẵn sàng nhận */
    ST_CLEARED,     /* vùng app đã xóa, chờ 0x1F50 */
    ST_RECEIVING,
    ST_DOWNLOADED,  /* nhận đủ, chờ Start */
    ST_DIRTY,       /* nạp dở / lỗi: cần Clear lại */
} prog_state_t;

static struct {
    bool layout_ok;
    bool app_valid;
    uint32_t sec_min;
    prog_state_t state;

    /* stream */
    uint32_t offset;       /* số byte đã nhận */
    uint32_t flash_len;    /* image_size + 4, hoặc dung lượng vùng app khi chưa có header */
    uint32_t sig_len;
    bool hdr_known;
    uint8_t hdr[sizeof(blc_app_header_t)];
    uint8_t dw[8];
    uint8_t magic_dw[8];   /* double-word chứa magic, chỉ ghi sau khi header đạt */
    uint32_t erased_end;   /* [APP_START, erased_end) đã xóa trong lần nạp này */
    uint8_t sig[SIG_MAX];
    uint32_t received;     /* tổng byte khi kết thúc transfer */
} P;

void program_init(bool layout_ok, bool app_valid, uint32_t sec_min)
{
    memset(&P, 0, sizeof P);
    P.layout_ok = layout_ok;
    P.app_valid = app_valid;
    P.sec_min = sec_min;
    P.state = ST_UNKNOWN;
}

bool program_app_valid(void) { return P.app_valid; }

uint8_t program_status(void) { return P.app_valid ? PROG_STATUS_STOPPED : PROG_STATUS_NO_PROGRAM; }

/* ---- Clear --------------------------------------------------------------- */

static bool page_is_blank(uint32_t addr)
{
    uint32_t ecc = blc_flash_ecc_events();
    const volatile uint32_t *p = (const volatile uint32_t *)addr;
    for (uint32_t i = 0; i < BLC_FLASH_PAGE_SIZE / 4u; i++) {
        if (p[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return blc_flash_ecc_events() == ecc;
}

/* Xóa page chứa addr nếu chưa trống, rồi kiểm tra lại. */
static bool erase_if_dirty(uint32_t addr)
{
    uint32_t page = addr & ~(BLC_FLASH_PAGE_SIZE - 1u);
    watchdog_kick();
    return page_is_blank(page) || (blc_flash_erase_page(page) && page_is_blank(page));
}

/* Xóa lazy: Clear chỉ xóa page đầu (vector table + header), nên app cũ mất
 * hiệu lực ngay và lệnh trả lời trong ~1 lần xóa page. Các page sau được xóa
 * khi stream chạm tới (ensure_erased), page chứa valid flag được xóa ngay khi
 * nhận đủ header (on_header_complete). */
static uint32_t do_clear(void)
{
    P.app_valid = false;
    P.state = ST_DIRTY;
    if (!erase_if_dirty(BLC_APP_START)) {
        return SDO_ABORT_HW_ERROR;
    }
    P.erased_end = BLC_APP_START + BLC_FLASH_PAGE_SIZE;
    P.state = ST_CLEARED;
    return SDO_OK;
}

/* Đảm bảo mọi page tới (và gồm) page chứa addr đã được xóa. Stream đi tuần tự
 * nên mỗi block/segment chạm tối đa 1 page mới (~22–40 ms). */
static bool ensure_erased(uint32_t addr)
{
    while (P.erased_end <= addr) {
        if (!erase_if_dirty(P.erased_end)) {
            return false;
        }
        P.erased_end += BLC_FLASH_PAGE_SIZE;
    }
    return true;
}

/* ---- Start --------------------------------------------------------------- */

static uint32_t do_start(bool *reset_after)
{
    if (P.state == ST_DOWNLOADED) {
        blc_app_header_t hdr;
        img_result_t r = image_verify_download(P.received, P.sig, P.sec_min, &hdr);
        if (r != IMG_OK) {
            P.state = ST_DIRTY;
            return SDO_ABORT_DATA_STORE;
        }
        if (!image_write_flag(&hdr)) {
            P.state = ST_DIRTY;
            return SDO_ABORT_HW_ERROR;
        }
        /* Nâng SEC_MIN sau khi đã ghi valid flag. Hết entry OTP vẫn chạy app. */
        (void)blc_secver_raise(hdr.security_version);
        P.app_valid = true;
        P.state = ST_UNKNOWN;
    }
    if (P.state == ST_RECEIVING || !P.app_valid) {
        return SDO_ABORT_DEVICE_STATE;
    }
    /* Reset sạch: lúc boot, bootloader kiểm tra lại app rồi nhảy với trạng
     * ngoại vi như sau reset. */
    *reset_after = true;
    return SDO_OK;
}

uint32_t program_control(uint8_t cmd, bool *reset_after)
{
    switch (cmd) {
    case PROG_CTRL_STOP:
        return SDO_OK; /* đã ở bootloader */
    case PROG_CTRL_START:
        return do_start(reset_after);
    case PROG_CTRL_CLEAR:
        if (!P.layout_ok) {
            return SDO_ABORT_HW_ERROR;
        }
        if (P.state == ST_RECEIVING) {
            return SDO_ABORT_DEVICE_STATE;
        }
        return do_clear();
    default:
        return SDO_ABORT_VALUE_RANGE;
    }
}

/* ---- Stream 0x1F50:01 ---------------------------------------------------- */

uint32_t program_dl_begin(bool size_known, uint32_t size)
{
    if (P.state != ST_CLEARED) {
        return SDO_ABORT_DEVICE_STATE;
    }
    uint32_t cap = blc_layout_app_end() - BLC_APP_START;
    if (size_known && size > cap + SIG_MAX) {
        return SDO_ABORT_LEN_HIGH;
    }
    P.offset = 0;
    P.flash_len = cap;
    P.sig_len = 0;
    P.hdr_known = false;
    memset(P.dw, 0xFF, sizeof P.dw);
    memset(P.magic_dw, 0xFF, sizeof P.magic_dw);
    P.state = ST_RECEIVING;
    return SDO_OK;
}

#define MAGIC_DW_OFFSET (BLC_APP_HEADER_OFFSET & ~7u)

static bool program_dw(uint32_t addr, const uint8_t b[8])
{
    uint64_t v;
    memcpy(&v, b, sizeof v);
    return v == UINT64_MAX || blc_flash_program_dw(addr, v); /* toàn FF: đã trống sẵn */
}

static bool flush_dw(uint32_t offset_of_dw)
{
    uint32_t addr = BLC_APP_START + offset_of_dw;
    bool ok = ensure_erased(addr);
    if (ok && offset_of_dw == MAGIC_DW_OFFSET && !P.hdr_known) {
        memcpy(P.magic_dw, P.dw, sizeof P.dw); /* hoãn tới on_header_complete */
    } else if (ok) {
        ok = program_dw(addr, P.dw);
    }
    memset(P.dw, 0xFF, sizeof P.dw);
    return ok;
}

static uint32_t on_header_complete(void)
{
    blc_app_header_t h;
    memcpy(&h, P.hdr, sizeof h);
    blc_hdr_result_t r = blc_app_header_check(&h, blc_layout_app_end(), P.sec_min);
    if (r != BLC_HDR_OK) {
        return SDO_ABORT_DATA_STORE;
    }
    /* Page chứa FLAG_ADDR có thể còn "APPVALID" của một image cũ (xóa lazy
     * không động tới phần sau image cũ). Xóa nó TRƯỚC khi ghi magic: nếu ghi
     * magic trước, mất điện lúc này sẽ để lại header hợp lệ + flag cũ, và
     * CRC32 (tuyến tính) dễ bị giả để boot code chưa verify chữ ký. */
    uint32_t flag = blc_app_flag_addr(h.image_size);
    if (flag >= P.erased_end && !erase_if_dirty(flag)) {
        return SDO_ABORT_HW_ERROR;
    }
    if (!program_dw(BLC_APP_START + MAGIC_DW_OFFSET, P.magic_dw)) {
        return SDO_ABORT_HW_ERROR;
    }
    P.flash_len = h.image_size + 4u;
    P.sig_len = blc_sig_len(h.sig_algo);
    P.hdr_known = true;
    return SDO_OK;
}

uint32_t program_dl_data(const uint8_t *data, uint32_t len)
{
    if (P.state != ST_RECEIVING) {
        return SDO_ABORT_DEVICE_STATE;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t o = P.offset;
        uint8_t b = data[i];

        if (!P.hdr_known && o >= BLC_APP_HEADER_OFFSET && o < HDR_END) {
            P.hdr[o - BLC_APP_HEADER_OFFSET] = b;
        }
        if (o < P.flash_len) {
            P.dw[o & 7u] = b;
            if ((o & 7u) == 7u || o + 1u == P.flash_len) {
                if (!flush_dw(o & ~7u)) {
                    P.state = ST_DIRTY;
                    return SDO_ABORT_HW_ERROR;
                }
            }
        } else if (P.hdr_known && o < P.flash_len + P.sig_len) {
            P.sig[o - P.flash_len] = b;
        } else {
            P.state = ST_DIRTY;
            return SDO_ABORT_LEN_HIGH;
        }
        P.offset = o + 1u;

        if (!P.hdr_known && P.offset == HDR_END) {
            uint32_t ab = on_header_complete();
            if (ab != SDO_OK) {
                P.state = ST_DIRTY;
                return ab;
            }
        }
    }
    return SDO_OK;
}

uint32_t program_dl_end(void)
{
    if (P.state != ST_RECEIVING) {
        return SDO_ABORT_DEVICE_STATE;
    }
    P.received = P.offset;
    if (!P.hdr_known || P.received != P.flash_len + P.sig_len) {
        P.state = ST_DIRTY;
        return SDO_ABORT_LEN_LOW;
    }
    P.state = ST_DOWNLOADED;
    return SDO_OK;
}

void program_dl_abort(void)
{
    if (P.state == ST_RECEIVING) {
        P.state = ST_DIRTY;
    }
}
