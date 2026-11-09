#include "rescue.h"

#include "clock.h"
#include "program.h"

uint32_t rescue_window_ms(uint32_t bitrate)
{
    if (BL_RESCUE_WINDOW_MIN_MS == 0u) {
        return 0u;
    }
    if (bitrate == 0u) {
        return BL_RESCUE_WINDOW_MIN_MS;
    }
    /* 6 × 135 bit, đổi ra ms và làm tròn lên. */
    uint32_t frames_ms = (BL_RESCUE_FRAMES * BL_RESCUE_FRAME_BITS * 1000u + bitrate - 1u) / bitrate;
    return (frames_ms > BL_RESCUE_WINDOW_MIN_MS) ? frames_ms : BL_RESCUE_WINDOW_MIN_MS;
}

bool rescue_is_stop(const can_frame_t *f, uint8_t node_id)
{
    const uint8_t *d = f->data;
    return f->id == 0x600u + node_id && f->dlc == 8u &&
           (d[0] == 0x2Fu || d[0] == 0x22u) &&
           d[1] == 0x51u && d[2] == 0x1Fu && d[3] == 0x01u &&
           d[4] == PROG_CTRL_STOP;
}

bool rescue_wait(uint8_t node_id, uint32_t window_ms, can_frame_t *stop)
{
    uint32_t t0 = clock_ms();
    can_frame_t f;
    /* "<=": bộ đếm ms có thể nhảy ngay sau t0, nên chờ ít nhất window_ms. */
    while ((clock_ms() - t0) <= window_ms) {
        watchdog_kick();
        while (fdcan_rx(&f)) {
            if (rescue_is_stop(&f, node_id)) {
                *stop = f;
                return true;
            }
        }
    }
    return false;
}
