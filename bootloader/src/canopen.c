/** NMT slave + heartbeat producer + định tuyến SDO. */
#include "canopen.h"

#include <stdbool.h>

#include "clock.h"
#include "fdcan.h"
#include "od.h"
#include "sdo.h"
#include "stm32g4xx.h"

static uint8_t s_node;
static uint8_t s_state;
static uint32_t s_hb_last;

static void send_bootup(void)
{
    can_frame_t f = {.id = (uint16_t)(0x700u + s_node), .dlc = 1, .data = {0x00}};
    (void)fdcan_tx(&f);
    s_state = NMT_STATE_PREOP;
    s_hb_last = clock_ms();
}

static void reset_communication(void)
{
    sdo_reset();
    g_od_heartbeat_ms = 0u; /* giá trị mặc định của 0x1017 */
    send_bootup();
}

void co_init(uint8_t node_id)
{
    s_node = node_id;
    sdo_init(node_id);
    reset_communication();
}

static void wait_tx_then_reset(void)
{
    uint32_t t0 = clock_ms();
    while (!fdcan_tx_idle() && (clock_ms() - t0) < 50u) {
    }
    NVIC_SystemReset();
}

static void handle_nmt(const can_frame_t *f)
{
    if (f->dlc < 2u || (f->data[1] != 0u && f->data[1] != s_node)) {
        return;
    }
    switch (f->data[0]) {
    case 0x01: s_state = NMT_STATE_OPERATIONAL; break;
    case 0x02: s_state = NMT_STATE_STOPPED; sdo_reset(); break;
    case 0x80: s_state = NMT_STATE_PREOP; break;
    case 0x81: wait_tx_then_reset(); break;  /* Reset node: boot lại, vào app nếu hợp lệ */
    case 0x82: reset_communication(); break;
    default: break;
    }
}

void co_process(void)
{
    can_frame_t f;
    fdcan_poll();
    while (fdcan_rx(&f)) {
        if (f.id == 0x000u) {
            handle_nmt(&f);
        } else if (f.id == 0x600u + s_node && s_state != NMT_STATE_STOPPED) {
            sdo_handle(&f);
        }
        if (g_od_reset_request) {
            wait_tx_then_reset(); /* đã gửi response cho Start program */
        }
    }

    uint32_t now = clock_ms();
    sdo_tick(now);
    if (g_od_heartbeat_ms != 0u && (now - s_hb_last) >= g_od_heartbeat_ms) {
        s_hb_last = now;
        can_frame_t hb = {.id = (uint16_t)(0x700u + s_node), .dlc = 1, .data = {s_state}};
        (void)fdcan_tx(&hb);
    }
}
