#include "program.h"

#include <string.h>

#include "clock.h"
#include "image.h"
#include "pds/pds_app_header.h"
#include "pds/pds_flash.h"
#include "pds/pds_layout.h"
#include "pds/pds_secver.h"
#include "sdo_abort.h"

#define SIG_MAX 256u
#define HDR_END (PDS_APP_HEADER_OFFSET + sizeof(pds_app_header_t))

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
    uint8_t hdr[sizeof(pds_app_header_t)];
    uint8_t dw[8];
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
    uint32_t ecc = pds_flash_ecc_events();
    const volatile uint32_t *p = (const volatile uint32_t *)addr;
    for (uint32_t i = 0; i < PDS_FLASH_PAGE_SIZE / 4u; i++) {
        if (p[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return pds_flash_ecc_events() == ecc;
}

static uint32_t do_clear(void)
{
    P.app_valid = false;
    P.state = ST_DIRTY;
    /* Chỉ xóa page chưa trống để rút ngắn thời gian (mỗi page ~22 ms). */
    for (uint32_t a = PDS_APP_START; a < pds_layout_app_end(); a += PDS_FLASH_PAGE_SIZE) {
        watchdog_kick();
        if (!page_is_blank(a) && (!pds_flash_erase_page(a) || !page_is_blank(a))) {
            return SDO_ABORT_HW_ERROR;
        }
    }
    P.state = ST_CLEARED;
    return SDO_OK;
}

/* ---- Start --------------------------------------------------------------- */

static uint32_t do_start(bool *reset_after)
{
    if (P.state == ST_DOWNLOADED) {
        pds_app_header_t hdr;
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
        (void)pds_secver_raise(hdr.security_version);
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
    uint32_t cap = pds_layout_app_end() - PDS_APP_START;
    if (size_known && size > cap + SIG_MAX) {
        return SDO_ABORT_LEN_HIGH;
    }
    P.offset = 0;
    P.flash_len = cap;
    P.sig_len = 0;
    P.hdr_known = false;
    memset(P.dw, 0xFF, sizeof P.dw);
    P.state = ST_RECEIVING;
    return SDO_OK;
}

static bool flush_dw(uint32_t offset_of_dw)
{
    uint64_t v;
    memcpy(&v, P.dw, sizeof v);
    memset(P.dw, 0xFF, sizeof P.dw);
    if (v == UINT64_MAX) {
        return true; /* đã trống sẵn */
    }
    return pds_flash_program_dw(PDS_APP_START + offset_of_dw, v);
}

static uint32_t on_header_complete(void)
{
    pds_app_header_t h;
    memcpy(&h, P.hdr, sizeof h);
    pds_hdr_result_t r = pds_app_header_check(&h, pds_layout_app_end(), P.sec_min);
    if (r != PDS_HDR_OK) {
        return SDO_ABORT_DATA_STORE;
    }
    P.flash_len = h.image_size + 4u;
    P.sig_len = pds_sig_len(h.sig_algo);
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

        if (!P.hdr_known && o >= PDS_APP_HEADER_OFFSET && o < HDR_END) {
            P.hdr[o - PDS_APP_HEADER_OFFSET] = b;
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
