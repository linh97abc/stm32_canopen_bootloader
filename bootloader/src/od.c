#include "od.h"

#include <string.h>

#include "blc/blc_identity.h"
#include "program.h"
#include "sdo_abort.h"

uint16_t g_od_heartbeat_ms;
bool g_od_reset_request;

typedef enum { SINK_NONE, SINK_SMALL, SINK_PROGRAM } sink_t;

static struct {
    sink_t kind;
    uint16_t index;
    uint8_t sub;
    bool size_known;
    uint8_t obj_size;
    uint8_t buf[4];
    uint8_t len;
} W;

static void put_u32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

uint32_t od_read(uint16_t index, uint8_t sub, uint8_t buf[4], uint8_t *len)
{
    uint32_t v;
    uint8_t n = 4;
    switch (index) {
    case 0x1000:
        if (sub != 0) return SDO_ABORT_NO_SUBINDEX;
        v = BLC_ID_DEVICE_TYPE;
        break;
    case 0x1001:
        if (sub != 0) return SDO_ABORT_NO_SUBINDEX;
        v = program_app_valid() ? 0u : 0x01u; /* bit 0: generic error khi không có app */
        n = 1;
        break;
    case 0x1017:
        if (sub != 0) return SDO_ABORT_NO_SUBINDEX;
        v = g_od_heartbeat_ms;
        n = 2;
        break;
    case 0x1018:
        switch (sub) {
        case 0: v = 4; n = 1; break;
        case 1: v = BLC_ID_VENDOR_ID; break;
        case 2: v = BLC_ID_PRODUCT_CODE; break;
        case 3: v = BLC_ID_REVISION; break;
        case 4: v = blc_identity_serial(); break;
        default: return SDO_ABORT_NO_SUBINDEX;
        }
        break;
    case 0x1F50:
        if (sub == 0) { v = 1; n = 1; break; }
        return (sub == 1) ? SDO_ABORT_WRITE_ONLY : SDO_ABORT_NO_SUBINDEX;
    case 0x1F51:
        if (sub == 0) { v = 1; n = 1; break; }
        if (sub != 1) return SDO_ABORT_NO_SUBINDEX;
        v = program_status();
        n = 1;
        break;
    default:
        return SDO_ABORT_NO_OBJECT;
    }
    put_u32(buf, v);
    *len = n;
    return SDO_OK;
}

uint32_t od_write_begin(uint16_t index, uint8_t sub, bool size_known, uint32_t size)
{
    od_write_abort();
    W.index = index;
    W.sub = sub;
    W.size_known = size_known;
    W.len = 0;

    switch (index) {
    case 0x1000:
    case 0x1001:
    case 0x1018:
        return (index == 0x1018 && sub > 4) ? SDO_ABORT_NO_SUBINDEX : SDO_ABORT_READ_ONLY;
    case 0x1017:
        if (sub != 0) return SDO_ABORT_NO_SUBINDEX;
        W.obj_size = 2;
        break;
    case 0x1F50:
        if (sub == 0) return SDO_ABORT_READ_ONLY;
        if (sub != 1) return SDO_ABORT_NO_SUBINDEX;
        {
            uint32_t ab = program_dl_begin(size_known, size);
            if (ab == SDO_OK) {
                W.kind = SINK_PROGRAM;
            }
            return ab;
        }
    case 0x1F51:
        if (sub == 0) return SDO_ABORT_READ_ONLY;
        if (sub != 1) return SDO_ABORT_NO_SUBINDEX;
        W.obj_size = 1;
        break;
    default:
        return SDO_ABORT_NO_OBJECT;
    }
    if (size_known && size != W.obj_size) {
        return (size > W.obj_size) ? SDO_ABORT_LEN_HIGH : SDO_ABORT_LEN_LOW;
    }
    W.kind = SINK_SMALL;
    return SDO_OK;
}

uint32_t od_write_data(const uint8_t *data, uint32_t len)
{
    if (W.kind == SINK_PROGRAM) {
        uint32_t ab = program_dl_data(data, len);
        if (ab != SDO_OK) {
            W.kind = SINK_NONE;
        }
        return ab;
    }
    if (W.kind != SINK_SMALL) {
        return SDO_ABORT_GENERAL;
    }
    if (W.len + len > sizeof W.buf) {
        W.kind = SINK_NONE;
        return SDO_ABORT_LEN_HIGH;
    }
    memcpy(&W.buf[W.len], data, len);
    W.len = (uint8_t)(W.len + len);
    return SDO_OK;
}

uint32_t od_write_end(void)
{
    sink_t kind = W.kind;
    W.kind = SINK_NONE;
    if (kind == SINK_PROGRAM) {
        return program_dl_end();
    }
    if (kind != SINK_SMALL) {
        return SDO_ABORT_GENERAL;
    }
    /* Expedited không chỉ định độ dài: dữ liệu luôn 4 byte, lấy obj_size byte đầu. */
    if (W.len < W.obj_size || (W.size_known && W.len != W.obj_size)) {
        return (W.len < W.obj_size) ? SDO_ABORT_LEN_LOW : SDO_ABORT_LEN_HIGH;
    }
    switch (W.index) {
    case 0x1017:
        g_od_heartbeat_ms = (uint16_t)(W.buf[0] | (W.buf[1] << 8));
        return SDO_OK;
    case 0x1F51: {
        bool reset_after = false;
        uint32_t ab = program_control(W.buf[0], &reset_after);
        if (ab == SDO_OK && reset_after) {
            g_od_reset_request = true;
        }
        return ab;
    }
    default:
        return SDO_ABORT_GENERAL;
    }
}

void od_write_abort(void)
{
    if (W.kind == SINK_PROGRAM) {
        program_dl_abort();
    }
    W.kind = SINK_NONE;
}
