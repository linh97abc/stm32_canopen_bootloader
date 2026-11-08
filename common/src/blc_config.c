#include "blc/blc_config.h"

#include <string.h>

#include "blc/blc_crc.h"
#include "blc/blc_flash.h"
#include "blc/blc_layout.h"

#define REC_SIZE      8u
#define REC_COUNT     (BLC_CONFIG_SIZE / REC_SIZE)
#define TAG_BLANK     0xFFu
#define TAG_DELETED   0x00u
#define WRITE_RETRIES 4u

typedef struct {
    uint8_t tag;
    uint8_t data[BLC_CFG_DATA_LEN];
} cfg_entry_t;

static cfg_entry_t s_cache[BLC_CFG_MAX_IDS];
static unsigned s_cache_n;
static unsigned s_write_idx;   /* chỉ số ô trống đầu tiên sau log */
static bool s_ready;

/* ---- mã hóa bản ghi ------------------------------------------------------ */

static uint64_t rec_encode(uint8_t tag, const uint8_t data[BLC_CFG_DATA_LEN])
{
    uint8_t b[REC_SIZE];
    b[0] = tag;
    memcpy(&b[1], data, BLC_CFG_DATA_LEN);
    b[7] = blc_crc8(b, 7u);
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | b[i]; /* little-endian: byte 0 ở địa chỉ thấp */
    }
    return v;
}

static bool rec_decode(uint64_t v, cfg_entry_t *e)
{
    uint8_t b[REC_SIZE];
    for (unsigned i = 0; i < REC_SIZE; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    if (b[0] == TAG_BLANK || b[0] == TAG_DELETED || blc_crc8(b, 7u) != b[7]) {
        return false;
    }
    e->tag = b[0];
    memcpy(e->data, &b[1], BLC_CFG_DATA_LEN);
    return true;
}

static uint32_t rec_addr(unsigned idx) { return BLC_CONFIG_START + idx * REC_SIZE; }

/* ---- cache ---------------------------------------------------------------- */

static cfg_entry_t *cache_find(uint8_t tag)
{
    for (unsigned i = 0; i < s_cache_n; i++) {
        if (s_cache[i].tag == tag) {
            return &s_cache[i];
        }
    }
    return NULL;
}

/* Trả về false nếu cache đầy và tag là tag mới. */
static bool cache_put(const cfg_entry_t *e)
{
    cfg_entry_t *slot = cache_find(e->tag);
    if (slot == NULL) {
        if (s_cache_n >= BLC_CFG_MAX_IDS) {
            return false;
        }
        slot = &s_cache[s_cache_n++];
    }
    *slot = *e;
    return true;
}

/* ---- thao tác page --------------------------------------------------------- */

static bool page_reset(void)
{
    s_cache_n = 0;
    s_write_idx = 0;
    return blc_flash_erase_page(BLC_CONFIG_START);
}

/* Append 1 bản ghi, bỏ qua ô ghi hỏng. false nếu page đầy hoặc lỗi. */
static bool append(const cfg_entry_t *e)
{
    uint64_t v = rec_encode(e->tag, e->data);
    for (unsigned tries = 0; tries < WRITE_RETRIES && s_write_idx < REC_COUNT; tries++) {
        unsigned idx = s_write_idx++;
        uint64_t rb;
        if (blc_flash_program_dw(rec_addr(idx), v) && blc_flash_read_dw(rec_addr(idx), &rb) && rb == v) {
            return true;
        }
        /* Ô hỏng: cố vô hiệu hóa (ghi 0x00) rồi thử ô kế tiếp. */
        (void)blc_flash_program_dw(rec_addr(idx), 0u);
    }
    return false;
}

/* Gom bản ghi mới nhất (đang ở cache) vào page vừa xóa. */
static bool compact(void)
{
    cfg_entry_t saved[BLC_CFG_MAX_IDS];
    unsigned n = s_cache_n;
    memcpy(saved, s_cache, n * sizeof saved[0]);
    if (!page_reset()) {
        return false;
    }
    for (unsigned i = 0; i < n; i++) {
        if (!append(&saved[i])) {
            return false;
        }
        s_cache[s_cache_n++] = saved[i];
    }
    return true;
}

/* ---- API ---------------------------------------------------------------- */

blc_cfg_status_t blc_config_init(void)
{
    unsigned last_used = 0;      /* 1 + chỉ số ô không trống cuối cùng */
    unsigned ecc_count = 0;
    unsigned ecc_idx = 0;

    s_ready = false;
    s_cache_n = 0;

    for (unsigned i = 0; i < REC_COUNT; i++) {
        uint64_t v;
        if (!blc_flash_read_dw(rec_addr(i), &v)) {
            ecc_count++;
            ecc_idx = i;
            last_used = i + 1u;
            continue;
        }
        if (v == UINT64_MAX) {
            continue;
        }
        last_used = i + 1u;
        cfg_entry_t e;
        if (rec_decode(v, &e)) {
            (void)cache_put(&e); /* quá BLC_CFG_MAX_IDS: tag thừa bị bỏ qua */
        }
    }
    s_write_idx = last_used;

    blc_cfg_status_t st = BLC_CFG_OK;
    bool damaged = (ecc_count != 0u);
    if (ecc_count == 1u && ecc_idx + 1u == last_used) {
        /* Đúng 1 ô lỗi ở cuối log = mất điện lúc append: vô hiệu ô đó
         * (ghi toàn 0x00 luôn được phép trên G4). */
        uint64_t v;
        if (blc_flash_program_dw(rec_addr(ecc_idx), 0u) && blc_flash_read_dw(rec_addr(ecc_idx), &v)) {
            damaged = false;
            st = BLC_CFG_REPAIRED;
        }
    }
    if (damaged) {
        /* Lỗi ECC ở giữa log = mất điện lúc xóa/gom page: không tin được nữa. */
        if (!page_reset()) {
            return BLC_CFG_ERR_FLASH;
        }
        st = BLC_CFG_PAGE_RESET;
    }

    s_ready = true;
    return st;
}

bool blc_config_read(uint8_t id, uint8_t out[BLC_CFG_DATA_LEN])
{
    const cfg_entry_t *e = s_ready ? cache_find(id) : NULL;
    if (e == NULL) {
        return false;
    }
    memcpy(out, e->data, BLC_CFG_DATA_LEN);
    return true;
}

blc_cfg_status_t blc_config_write(uint8_t id, const void *data, uint8_t len)
{
    if (!s_ready) {
        return BLC_CFG_ERR_NOT_INIT;
    }
    if (id == TAG_BLANK || id == TAG_DELETED || len > BLC_CFG_DATA_LEN || (data == NULL && len != 0u)) {
        return BLC_CFG_ERR_PARAM;
    }
    cfg_entry_t e = {.tag = id};
    memset(e.data, 0xFF, sizeof e.data);
    if (len != 0u) {
        memcpy(e.data, data, len);
    }

    cfg_entry_t *cur = cache_find(id);
    if (cur != NULL && memcmp(cur->data, e.data, BLC_CFG_DATA_LEN) == 0) {
        return BLC_CFG_OK; /* không đổi: không ghi */
    }
    if (cur == NULL && s_cache_n >= BLC_CFG_MAX_IDS) {
        return BLC_CFG_ERR_NO_SPACE;
    }

    if (append(&e)) {
        (void)cache_put(&e);
        return BLC_CFG_OK;
    }
    /* Page đầy (hoặc toàn ô hỏng ở cuối): gom page, giá trị mới đưa vào cache trước. */
    (void)cache_put(&e);
    return compact() ? BLC_CFG_OK : BLC_CFG_ERR_FLASH;
}

blc_cfg_status_t blc_config_erase_all(void)
{
    if (!s_ready) {
        return BLC_CFG_ERR_NOT_INIT;
    }
    return page_reset() ? BLC_CFG_OK : BLC_CFG_ERR_FLASH;
}

uint32_t blc_config_get_u32(uint8_t id, uint32_t def)
{
    uint8_t d[BLC_CFG_DATA_LEN];
    if (!blc_config_read(id, d)) {
        return def;
    }
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

blc_cfg_status_t blc_config_set_u32(uint8_t id, uint32_t value)
{
    uint8_t d[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return blc_config_write(id, d, sizeof d);
}

/* ---- cấu hình dùng chung ------------------------------------------------- */

static bool node_id_valid(uint8_t id) { return id >= 1u && id <= 127u; }

uint8_t blc_config_node_id(void)
{
    uint8_t d[BLC_CFG_DATA_LEN];
    if (blc_config_read(BLC_CFG_ID_NODE_ID, d) && node_id_valid(d[0])) {
        return d[0];
    }
    return BLC_CFG_DEFAULT_NODE_ID;
}

blc_cfg_status_t blc_config_set_node_id(uint8_t node_id)
{
    if (!node_id_valid(node_id)) {
        return BLC_CFG_ERR_PARAM;
    }
    return blc_config_write(BLC_CFG_ID_NODE_ID, &node_id, 1u);
}

uint32_t blc_bitrate_from_idx(uint8_t idx)
{
    static const uint32_t table[] = {1000000u, 800000u, 500000u, 250000u, 125000u, 0u, 50000u, 20000u, 10000u};
    return (idx < sizeof table / sizeof table[0]) ? table[idx] : 0u;
}

uint8_t blc_config_bitrate_idx(void)
{
    uint8_t d[BLC_CFG_DATA_LEN];
    if (blc_config_read(BLC_CFG_ID_CAN_BITRATE, d) && blc_bitrate_from_idx(d[0]) != 0u) {
        return d[0];
    }
    return BLC_CFG_DEFAULT_BITRATE_IDX;
}

blc_cfg_status_t blc_config_set_bitrate_idx(uint8_t idx)
{
    if (blc_bitrate_from_idx(idx) == 0u) {
        return BLC_CFG_ERR_PARAM;
    }
    return blc_config_write(BLC_CFG_ID_CAN_BITRATE, &idx, 1u);
}
