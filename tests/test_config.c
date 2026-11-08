/* Test thư viện config (common/src/blc_config.c) trên flash giả. */
#include <string.h>

#include "fake_flash.h"
#include "blc/blc_config.h"
#include "blc/blc_crc.h"
#include "blc/blc_flash.h"
#include "blc/blc_layout.h"
#include "test_util.h"

#define REC(i) (BLC_CONFIG_START + 8u * (i))
#define NREC   (BLC_CONFIG_SIZE / 8u)

static uint64_t make_rec(uint8_t tag, uint8_t d0)
{
    uint8_t b[8] = {tag, d0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
    b[7] = blc_crc8(b, 7);
    uint64_t v;
    memcpy(&v, b, 8);
    return v;
}

static unsigned used_cells(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < NREC; i++) {
        uint64_t v;
        memcpy(&v, (void *)(uintptr_t)REC(i), 8);
        if (v != UINT64_MAX) n = i + 1;
    }
    return n;
}

static void test_crc8(void)
{
    /* CRC-8/I-432-1, check value "123456789" = 0xA1 */
    CHECK_EQ(blc_crc8("123456789", 9), 0xA1);
    uint8_t zeros[7] = {0};
    CHECK(blc_crc8(zeros, 7) != 0x00); /* bản ghi toàn 0 không bao giờ hợp lệ */
    CHECK_EQ(blc_crc32("123456789", 9), 0xCBF43926u);
}

static void test_defaults(void)
{
    fake_flash_reset();
    CHECK_EQ(blc_config_init(), BLC_CFG_OK);
    CHECK_EQ(blc_config_node_id(), BLC_CFG_DEFAULT_NODE_ID);
    CHECK_EQ(blc_config_bitrate_idx(), BLC_CFG_DEFAULT_BITRATE_IDX);
    uint8_t d[6];
    CHECK(!blc_config_read(BLC_CFG_ID_NODE_ID, d));
}

static void test_write_read_persist(void)
{
    fake_flash_reset();
    blc_config_init();
    CHECK_EQ(blc_config_set_node_id(5), BLC_CFG_OK);
    CHECK_EQ(blc_config_set_bitrate_idx(BLC_BITRATE_IDX_500K), BLC_CFG_OK);
    CHECK_EQ(blc_config_node_id(), 5);
    CHECK_EQ(used_cells(), 2);

    /* Không đổi -> không ghi thêm */
    CHECK_EQ(blc_config_set_node_id(5), BLC_CFG_OK);
    CHECK_EQ(used_cells(), 2);

    /* Giá trị mới nhất thắng */
    CHECK_EQ(blc_config_set_node_id(9), BLC_CFG_OK);
    CHECK_EQ(used_cells(), 3);

    /* "Reboot" */
    CHECK_EQ(blc_config_init(), BLC_CFG_OK);
    CHECK_EQ(blc_config_node_id(), 9);
    CHECK_EQ(blc_config_bitrate_idx(), BLC_BITRATE_IDX_500K);
    CHECK_EQ(blc_bitrate_from_idx(blc_config_bitrate_idx()), 500000);

    /* u32 tiện ích */
    CHECK_EQ(blc_config_set_u32(0x20, 0xDEADBEEF), BLC_CFG_OK);
    blc_config_init();
    CHECK_EQ(blc_config_get_u32(0x20, 0), 0xDEADBEEF);
    CHECK_EQ(blc_config_get_u32(0x21, 77), 77);
}

static void test_validation(void)
{
    fake_flash_reset();
    blc_config_init();
    CHECK_EQ(blc_config_set_node_id(0), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_set_node_id(128), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_set_bitrate_idx(5), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_set_bitrate_idx(9), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_write(0x00, "x", 1), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_write(0xFF, "x", 1), BLC_CFG_ERR_PARAM);
    CHECK_EQ(blc_config_write(0x30, "1234567", 7), BLC_CFG_ERR_PARAM);

    /* Bản ghi CRC đúng nhưng ngoài miền -> mặc định */
    fake_flash_reset();
    fake_flash_poke(REC(0), make_rec(BLC_CFG_ID_NODE_ID, 200));
    fake_flash_poke(REC(1), make_rec(BLC_CFG_ID_CAN_BITRATE, 5));
    blc_config_init();
    CHECK_EQ(blc_config_node_id(), BLC_CFG_DEFAULT_NODE_ID);
    CHECK_EQ(blc_config_bitrate_idx(), BLC_CFG_DEFAULT_BITRATE_IDX);

    /* CRC sai -> bỏ qua, lấy bản hợp lệ trước đó */
    fake_flash_reset();
    fake_flash_poke(REC(0), make_rec(BLC_CFG_ID_NODE_ID, 10));
    fake_flash_poke(REC(1), make_rec(BLC_CFG_ID_NODE_ID, 11) ^ (1ull << 20));
    CHECK_EQ(blc_config_init(), BLC_CFG_OK);
    CHECK_EQ(blc_config_node_id(), 10);
    /* Ô rác không bị ghi đè: bản ghi mới nằm sau nó */
    blc_config_set_node_id(12);
    CHECK_EQ(used_cells(), 3);
}

static void test_compaction(void)
{
    fake_flash_reset();
    blc_config_init();
    blc_config_set_bitrate_idx(BLC_BITRATE_IDX_125K);
    blc_config_write(0x42, "\x01\x02\x03", 3); /* tag riêng của app cũng phải được giữ */
    unsigned erases = g_fake_erase_count;
    for (unsigned i = 0; i < NREC + 10u; i++) {
        CHECK_EQ(blc_config_set_node_id((uint8_t)(1 + i % 100)), BLC_CFG_OK);
    }
    CHECK_EQ(g_fake_erase_count, erases + 1);
    uint8_t last = (uint8_t)(1 + (NREC + 9u) % 100);
    blc_config_init();
    CHECK_EQ(blc_config_node_id(), last);
    CHECK_EQ(blc_config_bitrate_idx(), BLC_BITRATE_IDX_125K);
    uint8_t d[6];
    CHECK(blc_config_read(0x42, d) && d[0] == 1 && d[2] == 3 && d[3] == 0xFF);
}

static void test_too_many_ids(void)
{
    fake_flash_reset();
    blc_config_init();
    for (unsigned i = 0; i < BLC_CFG_MAX_IDS; i++) {
        CHECK_EQ(blc_config_write((uint8_t)(0x10 + i), "a", 1), BLC_CFG_OK);
    }
    CHECK_EQ(blc_config_write((uint8_t)(0x10 + BLC_CFG_MAX_IDS), "a", 1), BLC_CFG_ERR_NO_SPACE);
    CHECK_EQ(blc_config_write(0x10, "b", 1), BLC_CFG_OK); /* tag cũ vẫn ghi được */
}

static void test_ecc_last_cell_repaired(void)
{
    fake_flash_reset();
    blc_config_init();
    blc_config_set_node_id(33);
    blc_config_set_node_id(34);
    /* Mất điện khi đang append bản ghi thứ 3 */
    fake_flash_poke(REC(2), 0x1234567812345678ull);
    fake_flash_set_ecc(REC(2));
    CHECK_EQ(blc_config_init(), BLC_CFG_REPAIRED);
    CHECK_EQ(blc_config_node_id(), 34);
    uint64_t v;
    CHECK(blc_flash_read_dw(REC(2), &v) && v == 0); /* đã vô hiệu bằng 0x00 */
    CHECK_EQ(blc_config_set_node_id(35), BLC_CFG_OK);
    CHECK_EQ(used_cells(), 4);
    CHECK_EQ(blc_config_init(), BLC_CFG_OK);
    CHECK_EQ(blc_config_node_id(), 35);
}

static void test_ecc_middle_page_reset(void)
{
    fake_flash_reset();
    blc_config_init();
    blc_config_set_node_id(50);
    blc_config_set_node_id(51);
    blc_config_set_node_id(52);
    /* Mất điện khi đang xóa page: lỗi rải rác */
    fake_flash_set_ecc(REC(0));
    fake_flash_set_ecc(REC(2));
    CHECK_EQ(blc_config_init(), BLC_CFG_PAGE_RESET);
    CHECK_EQ(blc_config_node_id(), BLC_CFG_DEFAULT_NODE_ID);
    CHECK_EQ(used_cells(), 0);

    /* 1 lỗi duy nhất nhưng không ở cuối log cũng coi là page hỏng */
    blc_config_set_node_id(60);
    blc_config_set_node_id(61);
    fake_flash_set_ecc(REC(0));
    CHECK_EQ(blc_config_init(), BLC_CFG_PAGE_RESET);
}

static void test_bad_cell_on_append(void)
{
    fake_flash_reset();
    blc_config_init();
    blc_config_set_node_id(6);
    /* Ô kế tiếp không ghi được: append phải nhảy qua ô đó */
    fake_flash_set_stuck(REC(1));
    CHECK_EQ(blc_config_set_node_id(7), BLC_CFG_OK);
    CHECK_EQ(used_cells(), 3);
    blc_config_init();
    CHECK_EQ(blc_config_node_id(), 7);
}

int main(void)
{
    fake_flash_init();
    test_crc8();
    test_defaults();
    test_write_read_persist();
    test_validation();
    test_compaction();
    test_too_many_ids();
    test_ecc_last_cell_repaired();
    test_ecc_middle_page_reset();
    test_bad_cell_on_append();
    TEST_DONE();
}
