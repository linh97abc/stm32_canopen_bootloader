/* Flash giả cho test host: tuân theo luật ghi của G4 (ghi double-word vào ô
 * trống, hoặc ghi toàn 0 đè lên ô đã ghi). */
#include "fake_flash.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "pds/pds_flash.h"
#include "pds/pds_layout.h"
#include "pds/pds_secver.h"

#define OTP_MAP_BASE 0x1FFF7000u
#define OTP_MAP_SIZE 0x1000u
#define MAX_ECC 64

static uint32_t s_ecc[MAX_ECC];
static unsigned s_ecc_n;
static uint32_t s_ecc_events;
static uint32_t s_stuck = UINT32_MAX;

void fake_flash_set_stuck(uint32_t addr) { s_stuck = addr & ~7u; }
unsigned g_fake_erase_count;

static void *map_at(uintptr_t addr, size_t len)
{
    void *p = mmap((void *)addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)addr) {
        perror("mmap");
        exit(2);
    }
    return p;
}

void fake_flash_init(void)
{
    map_at(PDS_FLASH_BASE, FAKE_FLASH_SIZE);
    map_at(OTP_MAP_BASE, OTP_MAP_SIZE);
    fake_flash_reset();
}

void fake_flash_reset(void)
{
    memset((void *)(uintptr_t)PDS_FLASH_BASE, 0xFF, FAKE_FLASH_SIZE);
    memset((void *)(uintptr_t)OTP_MAP_BASE, 0xFF, OTP_MAP_SIZE);
    s_ecc_n = 0;
    s_stuck = UINT32_MAX;
    g_fake_erase_count = 0;
}

static int ecc_find(uint32_t addr)
{
    for (unsigned i = 0; i < s_ecc_n; i++) {
        if (s_ecc[i] == (addr & ~7u)) return (int)i;
    }
    return -1;
}

static void ecc_remove(unsigned i) { s_ecc[i] = s_ecc[--s_ecc_n]; }

void fake_flash_set_ecc(uint32_t addr)
{
    if (ecc_find(addr) < 0 && s_ecc_n < MAX_ECC) s_ecc[s_ecc_n++] = addr & ~7u;
}

void fake_flash_poke(uint32_t addr, uint64_t v) { memcpy((void *)(uintptr_t)addr, &v, 8); }

/* ---- pds_layout ---- */
uint32_t pds_layout_flash_end(void) { return PDS_FLASH_BASE + FAKE_FLASH_SIZE; }
bool pds_layout_check(void) { return true; }

/* ---- pds_flash ---- */
uint32_t pds_flash_ecc_events(void) { return s_ecc_events; }
bool pds_flash_nmi_handler(void) { return false; }

static bool in_range(uint32_t addr)
{
    return (addr >= PDS_FLASH_BASE && addr + 8u <= PDS_FLASH_BASE + FAKE_FLASH_SIZE) ||
           (addr >= OTP_MAP_BASE && addr + 8u <= OTP_MAP_BASE + 0x400u);
}

bool pds_flash_read_dw(uint32_t addr, uint64_t *out)
{
    if (!in_range(addr)) abort();
    memcpy(out, (void *)(uintptr_t)addr, 8);
    if (ecc_find(addr) >= 0) {
        s_ecc_events++;
        return false;
    }
    return true;
}

bool pds_flash_program_dw(uint32_t addr, uint64_t value)
{
    if ((addr & 7u) || !in_range(addr)) return false;
    uint64_t cur;
    memcpy(&cur, (void *)(uintptr_t)addr, 8);
    int e = ecc_find(addr);
    if (value == 0u) {
        if (e >= 0) ecc_remove((unsigned)e);
    } else if (cur != UINT64_MAX || e >= 0 || addr == s_stuck) {
        return false; /* PROGERR */
    }
    memcpy((void *)(uintptr_t)addr, &value, 8);
    return true;
}

bool pds_flash_erase_page(uint32_t addr)
{
    if (addr < PDS_FLASH_BASE || addr >= PDS_FLASH_BASE + FAKE_FLASH_SIZE) return false;
    uint32_t base = addr & ~(PDS_FLASH_PAGE_SIZE - 1u);
    memset((void *)(uintptr_t)base, 0xFF, PDS_FLASH_PAGE_SIZE);
    for (unsigned i = 0; i < s_ecc_n;) {
        if (s_ecc[i] >= base && s_ecc[i] < base + PDS_FLASH_PAGE_SIZE) ecc_remove(i);
        else i++;
    }
    if (s_stuck >= base && s_stuck < base + PDS_FLASH_PAGE_SIZE) s_stuck = UINT32_MAX;
    g_fake_erase_count++;
    return true;
}
