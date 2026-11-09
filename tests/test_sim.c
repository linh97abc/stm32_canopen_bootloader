/*
 * Mô phỏng end-to-end quy trình nạp CiA 302-3 trên PC:
 * master giả -> SDO server -> OD -> program -> flash giả, rồi kiểm tra
 * valid flag, boot check, chữ ký, chống rollback (OTP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_flash.h"
#include "image.h"
#include "od.h"
#include "blc/blc_app_header.h"
#include "blc/blc_layout.h"
#include "blc/blc_secver.h"
#include "program.h"
#include "rescue.h"
#include "sdo.h"
#include "sdo_abort.h"
#include "test_util.h"

#define NODE 0x22u
#define FAIL_NO_RSP 0xFFFFFFFFu

extern uint32_t g_sim_ms;
extern can_frame_t g_sim_tx[32];
extern unsigned g_sim_tx_n;

typedef struct {
    uint8_t *data;
    uint32_t len;
} blob_t;

static const char *s_dir;

static blob_t load(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    blob_t b = {.len = (uint32_t)ftell(f)};
    fseek(f, 0, SEEK_SET);
    b.data = malloc(b.len + 64);
    if (fread(b.data, 1, b.len, f) != b.len) exit(2);
    fclose(f);
    return b;
}

/* ---- master giả ---------------------------------------------------------- */

static void send(const uint8_t d[8])
{
    can_frame_t f = {.id = 0x600 + NODE, .dlc = 8};
    memcpy(f.data, d, 8);
    g_sim_tx_n = 0;
    sdo_handle(&f);
}

static bool rsp(uint8_t out[8])
{
    if (g_sim_tx_n != 1u || g_sim_tx[0].id != 0x580 + NODE) return false;
    memcpy(out, g_sim_tx[0].data, 8);
    g_sim_tx_n = 0;
    return true;
}

static uint32_t abort_code(const uint8_t r[8])
{
    return (uint32_t)r[4] | ((uint32_t)r[5] << 8) | ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24);
}

static uint32_t sdo_write(uint16_t idx, uint8_t sub, uint32_t v, unsigned len)
{
    uint8_t d[8] = {(uint8_t)(0x23 | ((4 - len) << 2)), (uint8_t)idx, (uint8_t)(idx >> 8), sub,
                    (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    uint8_t r[8];
    send(d);
    if (!rsp(r)) return FAIL_NO_RSP;
    if (r[0] == 0x80) return abort_code(r);
    return (r[0] == 0x60) ? SDO_OK : FAIL_NO_RSP;
}

static uint32_t sdo_read(uint16_t idx, uint8_t sub, uint32_t *v)
{
    uint8_t d[8] = {0x40, (uint8_t)idx, (uint8_t)(idx >> 8), sub};
    uint8_t r[8];
    send(d);
    if (!rsp(r)) return FAIL_NO_RSP;
    if (r[0] == 0x80) return abort_code(r);
    unsigned len = 4 - ((r[0] >> 2) & 3);
    *v = 0;
    for (unsigned i = 0; i < len; i++) *v |= (uint32_t)r[4 + i] << (8 * i);
    return SDO_OK;
}

static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Block download. drop_seq != 0: bỏ không gửi segment đó 1 lần (giả lập mất frame).
 * stop_after != 0: master ngừng (gửi abort) khi đã gửi quá số byte này. */
static uint32_t block_write(uint16_t idx, uint8_t sub, const uint8_t *data, uint32_t len, unsigned drop_seq,
                            uint32_t stop_after)
{
    uint8_t d[8] = {0xC6, (uint8_t)idx, (uint8_t)(idx >> 8), sub,
                    (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
    uint8_t r[8];
    send(d);
    if (!rsp(r)) return FAIL_NO_RSP;
    if (r[0] == 0x80) return abort_code(r);
    if (r[0] != 0xA4 || r[4] == 0 || r[4] > 127) return FAIL_NO_RSP;
    unsigned blksize = r[4];
    uint32_t pos = 0;
    bool dropped = false;
    for (;;) {
        uint32_t start = pos;
        unsigned seq;
        bool last = false;
        for (seq = 1; seq <= blksize; seq++) {
            uint32_t off = start + (seq - 1) * 7;
            last = off + 7 >= len;
            uint8_t s[8] = {(uint8_t)(seq | (last ? 0x80 : 0))};
            memcpy(&s[1], data + off, last ? len - off : 7);
            if (stop_after && off > stop_after) {
                uint8_t ab[8] = {0x80, (uint8_t)idx, (uint8_t)(idx >> 8), sub, 0, 0, 0x04, 0x08};
                send(ab);
                return 0xDEAD;
            }
            if (!dropped && drop_seq == seq && start > 0) {
                dropped = true;
            } else {
                send(s);
                if (g_sim_tx_n && g_sim_tx[0].data[0] == 0x80) return abort_code(g_sim_tx[0].data);
            }
            if (last) break;
        }
        if (!rsp(r)) return FAIL_NO_RSP;
        if (r[0] == 0x80) return abort_code(r);
        if (r[0] != 0xA2) return FAIL_NO_RSP;
        pos = start + r[1] * 7u;
        blksize = r[2];
        if (last && r[1] == seq) break;
    }
    uint32_t last_len = len - (pos - 7);
    uint16_t crc = crc16(data, len);
    uint8_t e[8] = {(uint8_t)(0xC1 | ((7 - last_len) << 2)), (uint8_t)crc, (uint8_t)(crc >> 8)};
    send(e);
    if (!rsp(r)) return FAIL_NO_RSP;
    if (r[0] == 0x80) return abort_code(r);
    return (r[0] == 0xA1) ? SDO_OK : FAIL_NO_RSP;
}

static uint32_t segmented_write(uint16_t idx, uint8_t sub, const uint8_t *data, uint32_t len)
{
    uint8_t d[8] = {0x21, (uint8_t)idx, (uint8_t)(idx >> 8), sub,
                    (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
    uint8_t r[8];
    send(d);
    if (!rsp(r)) return FAIL_NO_RSP;
    if (r[0] == 0x80) return abort_code(r);
    unsigned t = 0;
    for (uint32_t pos = 0; pos < len;) {
        uint32_t n = (len - pos) < 7 ? len - pos : 7;
        bool c = pos + n == len;
        uint8_t s[8] = {(uint8_t)((t << 4) | ((7 - n) << 1) | (c ? 1 : 0))};
        memcpy(&s[1], data + pos, n);
        send(s);
        if (!rsp(r)) return FAIL_NO_RSP;
        if (r[0] == 0x80) return abort_code(r);
        if (r[0] != (0x20 | (t << 4))) return FAIL_NO_RSP;
        t ^= 1;
        pos += n;
    }
    return SDO_OK;
}

/* ---- kiểm tra ------------------------------------------------------------ */

static uint32_t img_size(const blob_t *b) { return ((const blc_app_header_t *)(b->data + 0x200))->image_size; }

static uint64_t flag_at(const blob_t *b)
{
    uint64_t v;
    memcpy(&v, (void *)(uintptr_t)blc_app_flag_addr(img_size(b)), 8);
    return v;
}

static void flashed_ok(const blob_t *b)
{
    uint32_t n = img_size(b) + 4;
    CHECK(memcmp((void *)(uintptr_t)BLC_APP_START, b->data, n) == 0);
    /* Chữ ký không được ghi vào flash; phần sau image_crc trong double-word là 0xFF */
    const uint8_t *after = (const uint8_t *)(uintptr_t)(BLC_APP_START + n);
    for (uint32_t i = 0; (BLC_APP_START + n + i) % 8; i++) CHECK_EQ(after[i], 0xFF);
}

static bool page_blank(uint32_t addr)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)(addr & ~(BLC_FLASH_PAGE_SIZE - 1u));
    for (uint32_t i = 0; i < BLC_FLASH_PAGE_SIZE; i++) {
        if (p[i] != 0xFF) return false;
    }
    return true;
}

static void test_od(void)
{
    uint32_t v;
    CHECK_EQ(sdo_read(0x1018, 0, &v), SDO_OK);
    CHECK_EQ(v, 4);
    CHECK_EQ(sdo_read(0x1018, 4, &v), SDO_OK);
    CHECK_EQ(v, 0x12345678);
    CHECK_EQ(sdo_read(0x1001, 0, &v), SDO_OK);
    CHECK_EQ(v, 1); /* chưa có app */
    CHECK_EQ(sdo_read(0x1F51, 1, &v), SDO_OK);
    CHECK_EQ(v, PROG_STATUS_NO_PROGRAM);
    CHECK_EQ(sdo_write(0x1017, 0, 1000, 2), SDO_OK);
    CHECK_EQ(sdo_read(0x1017, 0, &v), SDO_OK);
    CHECK_EQ(v, 1000);
    CHECK_EQ(sdo_write(0x1017, 0, 1000, 1), SDO_ABORT_LEN_LOW);
    CHECK_EQ(sdo_write(0x1000, 0, 1, 4), SDO_ABORT_READ_ONLY);
    CHECK_EQ(sdo_write(0x1F51, 1, 7, 1), SDO_ABORT_VALUE_RANGE);
    CHECK_EQ(sdo_write(0x1F51, 2, 0, 1), SDO_ABORT_NO_SUBINDEX);
    CHECK_EQ(sdo_read(0x1F50, 1, &v), SDO_ABORT_WRITE_ONLY);
    CHECK_EQ(sdo_read(0x2000, 0, &v), SDO_ABORT_NO_OBJECT);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_STOP, 1), SDO_OK);
    /* expedited không chỉ định độ dài (s = 0) */
    uint8_t d[8] = {0x22, 0x17, 0x10, 0, 0xF4, 0x01, 0, 0};
    uint8_t r[8];
    send(d);
    CHECK(rsp(r) && r[0] == 0x60);
    CHECK_EQ(sdo_read(0x1017, 0, &v), SDO_OK);
    CHECK_EQ(v, 500);
}

static void test_download_flow(void)
{
    blob_t v1 = load("v1.blcimg"), v2 = load("v2.blcimg"), v3 = load("v3.blcimg");
    blob_t bad = load("badcrc.blcimg"), wrong = load("wrongkey.blcimg");

    /* Chưa Clear -> không cho nạp */
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len, 0, 0), SDO_ABORT_DEVICE_STATE);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DEVICE_STATE);

    /* v1: block download có CRC, mất 1 segment ở block thứ 2 */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len, 40, 0), SDO_OK);
    flashed_ok(&v1);
    CHECK_EQ(flag_at(&v1), UINT64_MAX);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    CHECK(g_od_reset_request);
    g_od_reset_request = false;
    CHECK_EQ(flag_at(&v1), BLC_APP_VALID_FLAG);
    CHECK_EQ(image_check_boot(0), IMG_OK);
    CHECK_EQ(blc_secver_min(), 0);
    uint32_t v;
    CHECK_EQ(sdo_read(0x1F51, 1, &v), SDO_OK);
    CHECK_EQ(v, PROG_STATUS_STOPPED);
    CHECK_EQ(sdo_read(0x1001, 0, &v), SDO_OK);
    CHECK_EQ(v, 0);

    /* Start khi đã có app hợp lệ (không nạp) -> reset để boot vào app */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    CHECK(g_od_reset_request);
    g_od_reset_request = false;

    /* Clear (lazy) chỉ xóa page đầu: header/vector mất nên app cũ hết hiệu lực
     * ngay, dù valid flag cũ vẫn còn ở page sau */
    g_fake_erase_count = 0;
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(g_fake_erase_count, 1);
    CHECK(page_blank(BLC_APP_START));
    CHECK_EQ(image_check_boot(0), IMG_BAD_HEADER);
    CHECK_EQ(sdo_read(0x1F51, 1, &v), SDO_OK);
    CHECK_EQ(v, PROG_STATUS_NO_PROGRAM);
    /* Clear lần nữa: page đầu đã trống thì không xóa lại */
    g_fake_erase_count = 0;
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(g_fake_erase_count, 0);

    /* CRC sai -> Start bị abort, không có flag */
    CHECK_EQ(block_write(0x1F50, 1, bad.data, bad.len, 0, 0), SDO_OK);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DATA_STORE);
    CHECK_EQ(flag_at(&bad), UINT64_MAX);
    CHECK(!g_od_reset_request);

    /* Chữ ký bởi khóa khác -> abort */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(segmented_write(0x1F50, 1, wrong.data, wrong.len), SDO_OK);
    CHECK_EQ(image_check_boot(0), IMG_NOT_VALID); /* CRC đúng nhưng chưa có flag */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DATA_STORE);
    CHECK_EQ(flag_at(&wrong), UINT64_MAX);

    /* Nạp dở (master abort) -> Start bị từ chối, phải Clear lại */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len, 0, 3000), 0xDEAD);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DEVICE_STATE);
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len, 0, 0), SDO_ABORT_DEVICE_STATE);

    /* File dài hơn image + chữ ký -> abort */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    uint8_t *longer = malloc(v1.len + 8);
    memcpy(longer, v1.data, v1.len);
    memset(longer + v1.len, 0, 8);
    CHECK_EQ(segmented_write(0x1F50, 1, longer, v1.len + 8), SDO_ABORT_LEN_HIGH);
    free(longer);

    /* File thiếu chữ ký -> abort lúc kết thúc transfer */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len - 10, 0, 0), SDO_ABORT_LEN_LOW);

    /* v2 (security_version 2, image_size % 8 == 4): segmented download, SEC_MIN nâng lên 2 */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(segmented_write(0x1F50, 1, v2.data, v2.len), SDO_OK);
    flashed_ok(&v2);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    g_od_reset_request = false;
    CHECK_EQ(blc_app_flag_addr(img_size(&v2)), BLC_APP_START + img_size(&v2) + 4);
    CHECK_EQ(flag_at(&v2), BLC_APP_VALID_FLAG);
    CHECK_EQ(blc_secver_min(), 2);
    CHECK_EQ(image_check_boot(2), IMG_OK);
    CHECK_EQ(image_check_boot(3), IMG_ROLLBACK);

    /* "Reboot" với SEC_MIN = 2: v1 (sec 0) và v3 (sec 1) bị từ chối ngay khi nhận header */
    program_init(true, true, blc_secver_min());
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v3.data, v3.len, 0, 0), SDO_ABORT_DATA_STORE);
    CHECK_EQ(*(const uint32_t *)(uintptr_t)BLC_APP_HEADER_ADDR, 0xFFFFFFFFu); /* magic không được ghi */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DEVICE_STATE);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(segmented_write(0x1F50, 1, v1.data, v1.len), SDO_ABORT_DATA_STORE);

    /* Hạ phiên bản app nhưng cùng security_version: v2 nạp lại được */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v2.data, v2.len, 126, 0), SDO_OK);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    g_od_reset_request = false;
    CHECK_EQ(image_check_boot(blc_secver_min()), IMG_OK);

    free(v1.data); free(v2.data); free(v3.data); free(bad.data); free(wrong.data);
}

/* Vùng app còn dữ liệu cũ (firmware trước) ở các page sau page đầu. */
static void fill_old_firmware(void)
{
    for (uint32_t a = BLC_APP_START; a < BLC_FLASH_BASE + FAKE_FLASH_SIZE; a += 8) {
        fake_flash_poke(a, 0x0123456789ABCDEFull ^ a);
    }
}

static void test_lazy_erase(void)
{
    blob_t v1 = load("v1.blcimg");
    uint32_t flag = blc_app_flag_addr(img_size(&v1));
    uint32_t pages = (img_size(&v1) + 4 + BLC_FLASH_PAGE_SIZE - 1) / BLC_FLASH_PAGE_SIZE;

    /* Nạp đè lên firmware cũ: chỉ xóa đúng các page image dùng */
    fake_flash_reset();
    fill_old_firmware();
    program_init(true, false, 0);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(g_fake_erase_count, 1);
    CHECK_EQ(block_write(0x1F50, 1, v1.data, v1.len, 0, 0), SDO_OK);
    CHECK(g_fake_erase_count >= pages && g_fake_erase_count <= pages + 1); /* + page flag nếu tách riêng */
    flashed_ok(&v1);
    CHECK_EQ(flag_at(&v1), UINT64_MAX);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    g_od_reset_request = false;
    CHECK_EQ(image_check_boot(0), IMG_OK);
    CHECK(!page_blank(flag + BLC_FLASH_PAGE_SIZE)); /* page sau image không bị xóa */

    /* Flag "APPVALID" cũ nằm đúng FLAG_ADDR của image mới (nạp lại image cùng
     * kích thước). Magic chỉ được ghi sau khi page chứa flag đã xóa, nên nạp
     * dở + reset không bao giờ để lại header hợp lệ đi kèm flag cũ. */
    fake_flash_reset();
    fill_old_firmware();
    fake_flash_poke(flag, BLC_APP_VALID_FLAG);
    program_init(true, false, 0);
    CHECK_EQ(program_control(PROG_CTRL_CLEAR, &(bool){false}), SDO_OK);
    CHECK_EQ(program_dl_begin(true, v1.len), SDO_OK);
    CHECK_EQ(program_dl_data(v1.data, 0x21B), SDO_OK); /* thiếu 1 byte header */
    CHECK_EQ(*(const uint32_t *)(uintptr_t)BLC_APP_HEADER_ADDR, 0xFFFFFFFFu);
    CHECK_EQ(flag_at(&v1), BLC_APP_VALID_FLAG);
    CHECK_EQ(program_dl_data(v1.data + 0x21B, 1), SDO_OK); /* đủ header */
    CHECK_EQ(flag_at(&v1), UINT64_MAX);
    CHECK_EQ(*(const uint32_t *)(uintptr_t)BLC_APP_HEADER_ADDR, BLC_APP_HEADER_MAGIC);
    CHECK_EQ(program_dl_data(v1.data + 0x21C, 0x2000), SDO_OK);
    program_dl_abort(); /* mất điện / master hủy giữa chừng */
    CHECK(image_check_boot(0) != IMG_OK);

    /* Hủy giữa chừng rồi nạp lại từ đầu */
    CHECK_EQ(program_dl_begin(true, v1.len), SDO_ABORT_DEVICE_STATE);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(segmented_write(0x1F50, 1, v1.data, v1.len), SDO_OK);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    g_od_reset_request = false;
    CHECK_EQ(image_check_boot(0), IMG_OK);

    free(v1.data);
}

static void test_otp_full(void)
{
    /* Hết 32 entry OTP: không nâng được nữa nhưng không ghi tràn */
    fake_flash_reset();
    for (uint32_t i = 0; i < BLC_OTP_SECVER_ENTRIES; i++) {
        CHECK_EQ(blc_secver_raise(i + 1), BLC_SECVER_OK);
    }
    CHECK_EQ(blc_secver_min(), BLC_OTP_SECVER_ENTRIES);
    CHECK_EQ(blc_secver_raise(100), BLC_SECVER_FULL);
    CHECK_EQ(blc_secver_raise(5), BLC_SECVER_UNCHANGED);
    uint64_t next;
    memcpy(&next, (void *)(uintptr_t)(BLC_OTP_SECVER_BASE + 8 * BLC_OTP_SECVER_ENTRIES), 8);
    CHECK_EQ(next, UINT64_MAX);

    /* Entry lỗi ECC / không hợp lệ bị bỏ qua */
    fake_flash_reset();
    fake_flash_poke(BLC_OTP_SECVER_BASE, ((uint64_t)~7u << 32) | 7u);
    fake_flash_poke(BLC_OTP_SECVER_BASE + 8, 0x0000000900000009ull); /* ~ sai */
    fake_flash_poke(BLC_OTP_SECVER_BASE + 16, ((uint64_t)~20u << 32) | 20u);
    fake_flash_set_ecc(BLC_OTP_SECVER_BASE + 16);
    CHECK_EQ(blc_secver_min(), 7);
    CHECK_EQ(blc_secver_raise(8), BLC_SECVER_OK);
    CHECK_EQ(blc_secver_min(), 8);
}

static void test_timeout(void)
{
    fake_flash_reset();
    program_init(true, false, 0);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    uint8_t d[8] = {0x21, 0x50, 0x1F, 1, 0x00, 0x10, 0, 0};
    uint8_t r[8];
    send(d);
    CHECK(rsp(r) && r[0] == 0x60);
    g_sim_ms += SDO_TIMEOUT_MS + 1;
    g_sim_tx_n = 0;
    sdo_tick(g_sim_ms);
    CHECK(rsp(r) && r[0] == 0x80 && abort_code(r) == SDO_ABORT_TIMEOUT);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DEVICE_STATE);
}

/* ---- cửa sổ cứu node (design.md mục 3.1) --------------------------------- */

typedef struct {
    uint32_t at;
    can_frame_t f;
} sim_rx_t;
extern sim_rx_t g_sim_rx[16];
extern unsigned g_sim_rx_n, g_sim_rx_i;

static can_frame_t sdo_frame(uint8_t node, uint8_t cmd, uint16_t idx, uint8_t sub, uint8_t v)
{
    can_frame_t f = {.id = (uint16_t)(0x600u + node), .dlc = 8,
                     .data = {cmd, (uint8_t)idx, (uint8_t)(idx >> 8), sub, v, 0, 0, 0}};
    return f;
}

static bool stop_ok(uint8_t node, uint8_t cmd, uint16_t idx, uint8_t sub, uint8_t v)
{
    can_frame_t f = sdo_frame(node, cmd, idx, sub, v);
    return rescue_is_stop(&f, NODE);
}

static void rx_push(uint32_t at, can_frame_t f)
{
    g_sim_rx[g_sim_rx_n].at = at;
    g_sim_rx[g_sim_rx_n].f = f;
    g_sim_rx_n++;
}

static void test_rescue(void)
{
    /* T = max(20 ms, 6 × 135 bit). */
    CHECK_EQ(rescue_window_ms(1000000), 20);
    CHECK_EQ(rescue_window_ms(250000), 20);
    CHECK_EQ(rescue_window_ms(50000), 20);
    CHECK_EQ(rescue_window_ms(20000), 41);
    CHECK_EQ(rescue_window_ms(10000), 81);

    /* Chỉ nhận Stop tới đúng node, DLC 8. */
    CHECK(stop_ok(NODE, 0x2F, 0x1F51, 1, 0));
    CHECK(stop_ok(NODE, 0x22, 0x1F51, 1, 0));
    CHECK(!stop_ok(NODE, 0x2F, 0x1F51, 1, 1));
    CHECK(!stop_ok(NODE, 0x2F, 0x1F51, 2, 0));
    CHECK(!stop_ok(NODE, 0x2F, 0x1F50, 1, 0));
    CHECK(!stop_ok(NODE + 1, 0x2F, 0x1F51, 1, 0));
    can_frame_t short_f = sdo_frame(NODE, 0x2F, 0x1F51, 1, 0);
    short_f.dlc = 5;
    CHECK(!rescue_is_stop(&short_f, NODE));
    can_frame_t nmt = {.id = 0x000, .dlc = 2, .data = {0x81, NODE}};
    CHECK(!rescue_is_stop(&nmt, NODE));

    /* Cửa sổ nhận đúng những lệnh Stop mà SDO server trả OK, không hơn không kém. */
    const uint8_t cmds[] = {0x2F, 0x2B, 0x27, 0x23, 0x22, 0x21, 0x20};
    for (unsigned i = 0; i < sizeof cmds; i++) {
        program_init(true, true, 0);
        can_frame_t f = sdo_frame(NODE, cmds[i], 0x1F51, 1, 0);
        g_sim_tx_n = 0;
        sdo_handle(&f);
        bool server_ok = g_sim_tx_n == 1u && g_sim_tx[0].data[0] == 0x60 &&
                         (cmds[i] & 0x02u) != 0u; /* expedited, không phải initiate segmented */
        sdo_reset();
        CHECK_EQ(rescue_is_stop(&f, NODE), server_ok);
    }

    /* Không có Stop: hết đúng T, traffic khác không kéo dài cửa sổ. */
    can_frame_t got;
    g_sim_ms = 1000;
    g_sim_rx_n = g_sim_rx_i = 0;
    rx_push(1005, nmt);
    rx_push(1010, sdo_frame(NODE, 0x40, 0x1F51, 1, 0));
    rx_push(1018, sdo_frame(NODE + 1, 0x2F, 0x1F51, 1, 0));
    CHECK(!rescue_wait(NODE, 20, &got));
    CHECK(g_sim_ms - 1000 >= 20 && g_sim_ms - 1000 <= 22);
    CHECK_EQ(g_sim_rx_i, 3);

    /* Stop tới giữa cửa sổ, sau traffic khác: bắt được. */
    g_sim_ms = 2000;
    g_sim_rx_n = g_sim_rx_i = 0;
    rx_push(2003, nmt);
    rx_push(2015, sdo_frame(NODE, 0x22, 0x1F51, 1, 0));
    CHECK(rescue_wait(NODE, 20, &got));
    CHECK(got.data[0] == 0x22 && got.id == 0x600 + NODE);

    /* Stop tới sau khi hết cửa sổ: không bắt. */
    g_sim_ms = 3000;
    g_sim_rx_n = g_sim_rx_i = 0;
    rx_push(3025, sdo_frame(NODE, 0x2F, 0x1F51, 1, 0));
    CHECK(!rescue_wait(NODE, 20, &got));
    g_sim_rx_n = g_sim_rx_i = 0;
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : "build";
    fake_flash_init();
    program_init(true, false, 0);
    sdo_init(NODE);

    test_od();
    test_download_flow();
    test_lazy_erase();
    test_otp_full();
    test_timeout();
    test_rescue();
    TEST_DONE();
}
