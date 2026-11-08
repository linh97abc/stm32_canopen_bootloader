#include "pds/pds_secver.h"

#include "pds/pds_flash.h"

typedef enum { ENTRY_BLANK, ENTRY_VALID, ENTRY_USED } entry_kind_t;

static entry_kind_t read_entry(unsigned i, uint32_t *ver)
{
    uint64_t v;
    if (!pds_flash_read_dw(PDS_OTP_SECVER_BASE + 8u * i, &v)) {
        return ENTRY_USED; /* lỗi ECC: bỏ qua, coi như đã dùng */
    }
    if (v == UINT64_MAX) {
        return ENTRY_BLANK;
    }
    uint32_t lo = (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    if (hi == ~lo) {
        *ver = lo;
        return ENTRY_VALID;
    }
    return ENTRY_USED;
}

uint32_t pds_secver_min(void)
{
    uint32_t min = 0u;
    for (unsigned i = 0; i < PDS_OTP_SECVER_ENTRIES; i++) {
        uint32_t v;
        if (read_entry(i, &v) == ENTRY_VALID && v > min) {
            min = v;
        }
    }
    return min;
}

pds_secver_status_t pds_secver_raise(uint32_t ver)
{
    if (ver <= pds_secver_min() || ver == 0xFFFFFFFFu) {
        return PDS_SECVER_UNCHANGED;
    }
    for (unsigned i = 0; i < PDS_OTP_SECVER_ENTRIES; i++) {
        uint32_t dummy;
        if (read_entry(i, &dummy) != ENTRY_BLANK) {
            continue;
        }
        uint64_t entry = ((uint64_t)~ver << 32) | ver;
        if (pds_flash_program_dw(PDS_OTP_SECVER_BASE + 8u * i, entry)) {
            return (pds_secver_min() >= ver) ? PDS_SECVER_OK : PDS_SECVER_ERR_FLASH;
        }
        /* Ghi lỗi: entry này coi như đã dùng, thử entry kế tiếp. */
    }
    return PDS_SECVER_FULL;
}
