/* Stub phần cứng cho test mô phỏng bootloader trên PC. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "boot_jump.h"
#include "clock.h"
#include "fdcan.h"
#include "blc/blc_identity.h"
#include "blc/blc_layout.h"

uint32_t g_sim_ms;
can_frame_t g_sim_tx[32];
unsigned g_sim_tx_n;

uint32_t clock_ms(void) { return g_sim_ms; }
void watchdog_kick(void) {}

bool fdcan_tx(const can_frame_t *f)
{
    if (g_sim_tx_n < 32u) g_sim_tx[g_sim_tx_n++] = *f;
    return true;
}
bool fdcan_tx_idle(void) { return true; }

uint32_t blc_identity_serial(void) { return 0x12345678u; }

bool boot_vectors_ok(void)
{
    uint32_t sp = *(const volatile uint32_t *)BLC_APP_START;
    uint32_t pc = *(const volatile uint32_t *)(BLC_APP_START + 4u);
    return (sp >> 24) == 0x20u && (pc & 1u) && pc > BLC_APP_START && pc < blc_layout_app_end();
}
