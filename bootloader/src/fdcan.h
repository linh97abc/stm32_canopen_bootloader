#ifndef BL_FDCAN_H
#define BL_FDCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t id;     /* 11-bit */
    uint8_t dlc;
    uint8_t data[8];
} can_frame_t;

/**
 * Khởi tạo FDCAN1 ở chế độ CAN classic. Bộ lọc phần cứng chỉ nhận
 * NMT (0x000) và SDO request (0x600 + node_id) vào RX FIFO0; ngắt RF0N
 * chuyển frame sang ring buffer.
 */
bool fdcan_init(uint32_t kernel_hz, uint32_t bitrate, uint8_t node_id);

/** Lấy 1 frame từ ring buffer. */
bool fdcan_rx(can_frame_t *f);

/** Đưa frame vào TX FIFO (chờ tối đa ~10 ms nếu đầy). */
bool fdcan_tx(const can_frame_t *f);

/** true khi không còn frame nào chờ gửi. */
bool fdcan_tx_idle(void);

/** Gọi định kỳ: tự phục hồi khi bus-off. */
void fdcan_poll(void);

/** Dừng FDCAN, tắt ngắt và clock. */
void fdcan_deinit(void);

#endif
