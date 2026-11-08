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
#include "pds/pds_app_header.h"
#include "pds/pds_layout.h"
#include "pds/pds_secver.h"
#include "program.h"
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

static uint32_t img_size(const blob_t *b) { return ((const pds_app_header_t *)(b->data + 0x200))->image_size; }

static uint64_t flag_at(const blob_t *b)
{
    uint64_t v;
    memcpy(&v, (void *)(uintptr_t)pds_app_flag_addr(img_size(b)), 8);
    return v;
}

static void flashed_ok(const blob_t *b)
{
    uint32_t n = img_size(b) + 4;
    CHECK(memcmp((void *)(uintptr_t)PDS_APP_START, b->data, n) == 0);
    /* Chữ ký không được ghi vào flash; phần sau image_crc trong double-word là 0xFF */
    const uint8_t *after = (const uint8_t *)(uintptr_t)(PDS_APP_START + n);
    for (uint32_t i = 0; (PDS_APP_START + n + i) % 8; i++) CHECK_EQ(after[i], 0xFF);
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
    blob_t v1 = load("v1.pdsimg"), v2 = load("v2.pdsimg"), v3 = load("v3.pdsimg");
    blob_t bad = load("badcrc.pdsimg"), wrong = load("wrongkey.pdsimg");

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
    CHECK_EQ(flag_at(&v1), PDS_APP_VALID_FLAG);
    CHECK_EQ(image_check_boot(0), IMG_OK);
    CHECK_EQ(pds_secver_min(), 0);
    uint32_t v;
    CHECK_EQ(sdo_read(0x1F51, 1, &v), SDO_OK);
    CHECK_EQ(v, PROG_STATUS_STOPPED);
    CHECK_EQ(sdo_read(0x1001, 0, &v), SDO_OK);
    CHECK_EQ(v, 0);

    /* Start khi đã có app hợp lệ (không nạp) -> reset để boot vào app */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    CHECK(g_od_reset_request);
    g_od_reset_request = false;

    /* Clear xóa cả valid flag */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(flag_at(&v1), UINT64_MAX);
    CHECK(image_check_boot(0) != IMG_OK);

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
    CHECK_EQ(pds_app_flag_addr(img_size(&v2)), PDS_APP_START + img_size(&v2) + 4);
    CHECK_EQ(flag_at(&v2), PDS_APP_VALID_FLAG);
    CHECK_EQ(pds_secver_min(), 2);
    CHECK_EQ(image_check_boot(2), IMG_OK);
    CHECK_EQ(image_check_boot(3), IMG_ROLLBACK);

    /* "Reboot" với SEC_MIN = 2: v1 (sec 0) và v3 (sec 1) bị từ chối ngay khi nhận header */
    program_init(true, true, pds_secver_min());
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v3.data, v3.len, 0, 0), SDO_ABORT_DATA_STORE);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_ABORT_DEVICE_STATE);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(segmented_write(0x1F50, 1, v1.data, v1.len), SDO_ABORT_DATA_STORE);

    /* Hạ phiên bản app nhưng cùng security_version: v2 nạp lại được */
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_CLEAR, 1), SDO_OK);
    CHECK_EQ(block_write(0x1F50, 1, v2.data, v2.len, 126, 0), SDO_OK);
    CHECK_EQ(sdo_write(0x1F51, 1, PROG_CTRL_START, 1), SDO_OK);
    g_od_reset_request = false;
    CHECK_EQ(image_check_boot(pds_secver_min()), IMG_OK);

    free(v1.data); free(v2.data); free(v3.data); free(bad.data); free(wrong.data);
}

static void test_otp_full(void)
{
    /* Hết 32 entry OTP: không nâng được nữa nhưng không ghi tràn */
    fake_flash_reset();
    for (uint32_t i = 0; i < PDS_OTP_SECVER_ENTRIES; i++) {
        CHECK_EQ(pds_secver_raise(i + 1), PDS_SECVER_OK);
    }
    CHECK_EQ(pds_secver_min(), PDS_OTP_SECVER_ENTRIES);
    CHECK_EQ(pds_secver_raise(100), PDS_SECVER_FULL);
    CHECK_EQ(pds_secver_raise(5), PDS_SECVER_UNCHANGED);
    uint64_t next;
    memcpy(&next, (void *)(uintptr_t)(PDS_OTP_SECVER_BASE + 8 * PDS_OTP_SECVER_ENTRIES), 8);
    CHECK_EQ(next, UINT64_MAX);

    /* Entry lỗi ECC / không hợp lệ bị bỏ qua */
    fake_flash_reset();
    fake_flash_poke(PDS_OTP_SECVER_BASE, ((uint64_t)~7u << 32) | 7u);
    fake_flash_poke(PDS_OTP_SECVER_BASE + 8, 0x0000000900000009ull); /* ~ sai */
    fake_flash_poke(PDS_OTP_SECVER_BASE + 16, ((uint64_t)~20u << 32) | 20u);
    fake_flash_set_ecc(PDS_OTP_SECVER_BASE + 16);
    CHECK_EQ(pds_secver_min(), 7);
    CHECK_EQ(pds_secver_raise(8), PDS_SECVER_OK);
    CHECK_EQ(pds_secver_min(), 8);
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

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : "build";
    fake_flash_init();
    program_init(true, false, 0);
    sdo_init(NODE);

    test_od();
    test_download_flow();
    test_otp_full();
    test_timeout();
    TEST_DONE();
}
