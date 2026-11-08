/**
 * SDO server (CiA 301): upload expedited; download expedited, segmented và
 * block (có CRC-16 tùy chọn).
 *
 * Block download: segment được gom vào buffer RAM; khi hết block thì ghi flash
 * TRƯỚC rồi mới gửi block ACK. Client luôn chờ ACK trước khi gửi block kế
 * tiếp, nên trong lúc CPU bị stall vì ghi flash không có frame nào bị mất.
 */
#include "sdo.h"

#include <string.h>

#include "clock.h"
#include "od.h"
#include "sdo_abort.h"

typedef enum { S_IDLE, S_DL_SEG, S_BLK_DATA, S_BLK_END } sdo_state_t;

static struct {
    uint8_t node;
    sdo_state_t st;
    uint16_t index;
    uint8_t sub;
    uint32_t last_ms;
    /* segmented */
    uint8_t toggle;
    /* block */
    bool crc_on;
    uint16_t crc;
    uint8_t expected;      /* seqno mong đợi tiếp theo */
    uint8_t ackseq;        /* seqno cuối cùng nhận đúng thứ tự */
    bool last_seen;
    uint8_t blk[SDO_BLKSIZE * 7u];
    uint8_t pending[7];    /* segment cuối, chờ biết n từ End block */
} S;

static uint16_t crc16_ccitt(uint16_t crc, const uint8_t *p, uint32_t n)
{
    static const uint16_t tab[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
                                     0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF};
    while (n--) {
        uint8_t b = *p++;
        crc = (uint16_t)((crc << 4) ^ tab[((crc >> 12) ^ (b >> 4)) & 0xFu]);
        crc = (uint16_t)((crc << 4) ^ tab[((crc >> 12) ^ (b & 0xFu)) & 0xFu]);
    }
    return crc;
}

static void send(const uint8_t d[8])
{
    can_frame_t f = {.id = (uint16_t)(0x580u + S.node), .dlc = 8};
    memcpy(f.data, d, 8);
    (void)fdcan_tx(&f);
    S.last_ms = clock_ms();
}

static void send_mux(uint8_t cmd, uint16_t index, uint8_t sub, uint32_t v)
{
    uint8_t d[8] = {cmd, (uint8_t)index, (uint8_t)(index >> 8), sub,
                    (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    send(d);
}

static void abort_transfer(uint16_t index, uint8_t sub, uint32_t code)
{
    od_write_abort();
    S.st = S_IDLE;
    send_mux(0x80, index, sub, code);
}

void sdo_init(uint8_t node_id)
{
    memset(&S, 0, sizeof S);
    S.node = node_id;
}

void sdo_reset(void)
{
    od_write_abort();
    S.st = S_IDLE;
}

void sdo_tick(uint32_t now_ms)
{
    if (S.st != S_IDLE && (now_ms - S.last_ms) > SDO_TIMEOUT_MS) {
        abort_transfer(S.index, S.sub, SDO_ABORT_TIMEOUT);
    }
}

/* ---- block download -------------------------------------------------- */

static uint32_t blk_commit(const uint8_t *p, uint32_t n)
{
    if (n == 0u) {
        return SDO_OK;
    }
    if (S.crc_on) {
        S.crc = crc16_ccitt(S.crc, p, n);
    }
    return od_write_data(p, n);
}

static void blk_segment(const uint8_t *d)
{
    uint8_t seq = d[0] & 0x7Fu;
    bool last = (d[0] & 0x80u) != 0u;

    if (seq == S.expected && seq >= 1u && seq <= SDO_BLKSIZE) {
        memcpy(&S.blk[(seq - 1u) * 7u], &d[1], 7);
        S.ackseq = seq;
        S.expected = (uint8_t)(seq + 1u);
        S.last_seen = last;
    }
    if (seq != SDO_BLKSIZE && !last) {
        return; /* chưa hết block */
    }

    /* Hết block: ghi phần nhận đúng thứ tự (giữ lại segment cuối nếu là cuối transfer). */
    uint32_t n = (uint32_t)S.ackseq * 7u;
    if (S.last_seen) {
        n -= 7u;
        memcpy(S.pending, &S.blk[n], 7);
    }
    uint32_t ab = blk_commit(S.blk, n);
    if (ab != SDO_OK) {
        abort_transfer(S.index, S.sub, ab);
        return;
    }
    uint8_t ack[8] = {0xA2, S.ackseq, SDO_BLKSIZE, 0, 0, 0, 0, 0};
    send(ack);
    S.expected = 1u;
    S.ackseq = 0u;
    if (S.last_seen) {
        S.st = S_BLK_END;
    }
}

static void blk_end(const uint8_t *d)
{
    if ((d[0] & 0xE3u) != 0xC1u) {
        abort_transfer(S.index, S.sub, SDO_ABORT_CMD);
        return;
    }
    uint8_t n = (d[0] >> 2) & 7u;
    uint32_t ab = blk_commit(S.pending, 7u - n);
    if (ab == SDO_OK && S.crc_on && S.crc != (uint16_t)(d[1] | (d[2] << 8))) {
        ab = SDO_ABORT_CRC;
    }
    if (ab == SDO_OK) {
        ab = od_write_end();
    }
    if (ab != SDO_OK) {
        abort_transfer(S.index, S.sub, ab);
        return;
    }
    uint8_t rsp[8] = {0xA1, 0, 0, 0, 0, 0, 0, 0};
    S.st = S_IDLE;
    send(rsp);
}

/* ---- xử lý request -------------------------------------------------------- */

static void initiate_download(const uint8_t *d, uint16_t index, uint8_t sub)
{
    bool e = (d[0] & 0x02u) != 0u;
    bool s = (d[0] & 0x01u) != 0u;
    uint32_t ab;

    if (e) {
        uint32_t len = s ? 4u - ((d[0] >> 2) & 3u) : 4u;
        ab = od_write_begin(index, sub, s, len);
        if (ab == SDO_OK) ab = od_write_data(&d[4], len);
        if (ab == SDO_OK) ab = od_write_end();
        if (ab != SDO_OK) {
            abort_transfer(index, sub, ab);
            return;
        }
        send_mux(0x60, index, sub, 0);
        return;
    }
    uint32_t size = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    ab = od_write_begin(index, sub, s, size);
    if (ab != SDO_OK) {
        abort_transfer(index, sub, ab);
        return;
    }
    S.st = S_DL_SEG;
    S.toggle = 0;
    send_mux(0x60, index, sub, 0);
}

static void download_segment(const uint8_t *d)
{
    uint8_t t = (d[0] >> 4) & 1u;
    if (t != S.toggle) {
        abort_transfer(S.index, S.sub, SDO_ABORT_TOGGLE);
        return;
    }
    uint32_t len = 7u - ((d[0] >> 1) & 7u);
    bool c = (d[0] & 1u) != 0u;
    uint32_t ab = od_write_data(&d[1], len);
    if (ab == SDO_OK && c) {
        ab = od_write_end();
    }
    if (ab != SDO_OK) {
        abort_transfer(S.index, S.sub, ab);
        return;
    }
    uint8_t rsp[8] = {(uint8_t)(0x20u | (t << 4)), 0, 0, 0, 0, 0, 0, 0};
    S.toggle ^= 1u;
    if (c) {
        S.st = S_IDLE;
    }
    send(rsp);
}

static void initiate_block_download(const uint8_t *d, uint16_t index, uint8_t sub)
{
    if ((d[0] & 0x01u) != 0u) { /* cs phải = 0 (initiate) */
        abort_transfer(index, sub, SDO_ABORT_CMD);
        return;
    }
    bool s = (d[0] & 0x02u) != 0u;
    uint32_t size = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    uint32_t ab = od_write_begin(index, sub, s, size);
    if (ab != SDO_OK) {
        abort_transfer(index, sub, ab);
        return;
    }
    S.crc_on = (d[0] & 0x04u) != 0u;
    S.crc = 0;
    S.expected = 1u;
    S.ackseq = 0u;
    S.last_seen = false;
    S.st = S_BLK_DATA;
    uint8_t rsp[8] = {0xA4 /* scs=5, sc=1 */, (uint8_t)index, (uint8_t)(index >> 8), sub, SDO_BLKSIZE, 0, 0, 0};
    send(rsp);
}

static void upload(uint16_t index, uint8_t sub)
{
    uint8_t buf[4] = {0};
    uint8_t len = 0;
    uint32_t ab = od_read(index, sub, buf, &len);
    if (ab != SDO_OK) {
        send_mux(0x80, index, sub, ab);
        return;
    }
    uint8_t d[8] = {(uint8_t)(0x43u | ((4u - len) << 2)), (uint8_t)index, (uint8_t)(index >> 8), sub,
                    buf[0], buf[1], buf[2], buf[3]};
    send(d);
}

void sdo_handle(const can_frame_t *rx)
{
    uint8_t d[8] = {0};
    memcpy(d, rx->data, rx->dlc);
    S.last_ms = clock_ms();

    if (d[0] == 0x80u) { /* client abort */
        sdo_reset();
        return;
    }
    if (S.st == S_BLK_DATA) {
        blk_segment(d);
        return;
    }
    if (S.st == S_BLK_END) {
        blk_end(d);
        return;
    }

    uint8_t ccs = d[0] >> 5;
    uint16_t index = (uint16_t)(d[1] | (d[2] << 8));
    uint8_t sub = d[3];

    if (S.st == S_DL_SEG && ccs == 0u) {
        download_segment(d);
        return;
    }
    /* Request mới làm hủy transfer cũ (nếu có). */
    sdo_reset();
    S.index = index;
    S.sub = sub;

    switch (ccs) {
    case 1: initiate_download(d, index, sub); break;
    case 2: upload(index, sub); break;
    case 6: initiate_block_download(d, index, sub); break;
    default: send_mux(0x80, index, sub, SDO_ABORT_CMD); break; /* segmented/block upload không hỗ trợ */
    }
}
