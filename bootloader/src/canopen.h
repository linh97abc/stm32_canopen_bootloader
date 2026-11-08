#ifndef BL_CANOPEN_H
#define BL_CANOPEN_H

#include <stdint.h>

#define NMT_STATE_STOPPED     0x04u
#define NMT_STATE_OPERATIONAL 0x05u
#define NMT_STATE_PREOP       0x7Fu

/** Gửi boot-up, vào pre-operational. */
void co_init(uint8_t node_id);

/** Gọi liên tục trong main loop. */
void co_process(void);

#endif
