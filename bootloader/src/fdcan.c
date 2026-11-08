/**
 * FDCAN1 (STM32G4) ở mức thanh ghi: LL không hỗ trợ FDCAN.
 *
 * Message RAM của G4 có bố cục cố định (RM0440, mục FDCAN):
 *   28 std filter | 8 ext filter | RX FIFO0 (3) | RX FIFO1 (3) | TX event (3) | TX FIFO (3)
 */
#include "fdcan.h"

#include <string.h>

#include "board.h"
#include "clock.h"
#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"

#define CAN_INST          FDCAN1
#define MRAM_BASE         SRAMCAN_BASE      /* FDCAN1 dùng khối đầu tiên */
#define MRAM_FLS          (MRAM_BASE + 0x000u)
#define MRAM_RF0          (MRAM_BASE + 0x0B0u)
#define MRAM_TXB          (MRAM_BASE + 0x278u)
#define MRAM_SIZE         0x350u
#define ELEM_SIZE         72u               /* 18 word mỗi phần tử RX/TX */

#define RX_RING_SIZE      64u               /* lũy thừa của 2 */

static can_frame_t s_ring[RX_RING_SIZE];
static volatile uint32_t s_head, s_tail;
static volatile uint32_t s_overruns;

static bool wait_cccr(uint32_t mask, bool set)
{
    for (uint32_t t = 0; t < 1000000u; t++) {
        if (((CAN_INST->CCCR & mask) != 0u) == set) {
            return true;
        }
    }
    return false;
}

/* Chọn prescaler + số tq (8..25), điểm lấy mẫu ~87.5% (CiA 301). */
static bool calc_timing(uint32_t clk, uint32_t bitrate, uint32_t *nbtp)
{
    for (uint32_t tq = 25u; tq >= 8u; tq--) {
        uint32_t div = bitrate * tq;
        if (clk % div != 0u) {
            continue;
        }
        uint32_t brp = clk / div;
        if (brp < 1u || brp > 512u) {
            continue;
        }
        uint32_t sp = (tq * 875u + 500u) / 1000u;   /* số tq tới điểm lấy mẫu */
        uint32_t seg1 = sp - 1u;                    /* trừ sync segment */
        uint32_t seg2 = tq - sp;
        uint32_t sjw = (seg2 < 4u) ? seg2 : 4u;
        *nbtp = ((sjw - 1u) << FDCAN_NBTP_NSJW_Pos) | ((brp - 1u) << FDCAN_NBTP_NBRP_Pos) |
                ((seg1 - 1u) << FDCAN_NBTP_NTSEG1_Pos) | ((seg2 - 1u) << FDCAN_NBTP_NTSEG2_Pos);
        return true;
    }
    return false;
}

static void gpio_init(void)
{
    RCC->AHB2ENR |= BOARD_CAN_GPIO_CLK;
    (void)RCC->AHB2ENR;
    const uint32_t pins[2] = {BOARD_CAN_RX_PIN, BOARD_CAN_TX_PIN};
    for (unsigned i = 0; i < 2u; i++) {
        uint32_t pin = pins[i];
        LL_GPIO_SetPinMode(BOARD_CAN_GPIO, pin, LL_GPIO_MODE_ALTERNATE);
        LL_GPIO_SetPinSpeed(BOARD_CAN_GPIO, pin, LL_GPIO_SPEED_FREQ_HIGH);
        LL_GPIO_SetPinOutputType(BOARD_CAN_GPIO, pin, LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinPull(BOARD_CAN_GPIO, pin, LL_GPIO_PULL_NO);
        if (pin <= LL_GPIO_PIN_7) {
            LL_GPIO_SetAFPin_0_7(BOARD_CAN_GPIO, pin, BOARD_CAN_AF);
        } else {
            LL_GPIO_SetAFPin_8_15(BOARD_CAN_GPIO, pin, BOARD_CAN_AF);
        }
    }
#ifdef BOARD_CAN_STBY_GPIO
    RCC->AHB2ENR |= BOARD_CAN_STBY_GPIO_CLK;
    (void)RCC->AHB2ENR;
    if (BOARD_CAN_STBY_ACTIVE) {
        LL_GPIO_SetOutputPin(BOARD_CAN_STBY_GPIO, BOARD_CAN_STBY_PIN);
    } else {
        LL_GPIO_ResetOutputPin(BOARD_CAN_STBY_GPIO, BOARD_CAN_STBY_PIN);
    }
    LL_GPIO_SetPinMode(BOARD_CAN_STBY_GPIO, BOARD_CAN_STBY_PIN, LL_GPIO_MODE_OUTPUT);
#endif
}

bool fdcan_init(uint32_t kernel_hz, uint32_t bitrate, uint8_t node_id)
{
    uint32_t nbtp;
    if (!calc_timing(kernel_hz, bitrate, &nbtp)) {
        return false;
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_FDCAN);
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_FDCAN);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_FDCAN);
    gpio_init();

    CAN_INST->CCCR &= ~FDCAN_CCCR_CSR;          /* thoát sleep */
    CAN_INST->CCCR |= FDCAN_CCCR_INIT;
    if (!wait_cccr(FDCAN_CCCR_INIT, true)) {
        return false;
    }
    CAN_INST->CCCR |= FDCAN_CCCR_CCE;
    CAN_INST->CCCR &= ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_DAR | FDCAN_CCCR_TEST |
                        FDCAN_CCCR_MON | FDCAN_CCCR_ASM);
    FDCAN_CONFIG->CKDIV = 0u;                    /* kernel clock / 1 */
    CAN_INST->NBTP = nbtp;

    /* Xóa message RAM (chỉ được ghi theo word). */
    for (uint32_t off = 0; off < MRAM_SIZE; off += 4u) {
        *(volatile uint32_t *)(MRAM_BASE + off) = 0u;
    }

    /* Filter std #0: dual ID {0x000 (NMT), 0x600+node (SDO rx)} -> FIFO0. */
    *(volatile uint32_t *)MRAM_FLS = (1UL << 30) | (1UL << 27) | (0x000UL << 16) | (0x600UL + node_id);
    CAN_INST->RXGFC = (1UL << FDCAN_RXGFC_LSS_Pos) | (0UL << FDCAN_RXGFC_LSE_Pos) |
                      (2UL << FDCAN_RXGFC_ANFS_Pos) | (2UL << FDCAN_RXGFC_ANFE_Pos) |
                      (1UL << FDCAN_RXGFC_RRFS_Pos) | (1UL << FDCAN_RXGFC_RRFE_Pos);

    CAN_INST->TXBC = 0u;                         /* TX FIFO mode */
    CAN_INST->IR = 0xFFFFFFFFu;
    CAN_INST->IE = FDCAN_IE_RF0NE;
    CAN_INST->ILS = 0u;                          /* mọi nhóm -> line 0 */
    CAN_INST->ILE = FDCAN_ILE_EINT0;

    s_head = s_tail = 0u;
    NVIC_SetPriority(FDCAN1_IT0_IRQn, 1u);
    NVIC_ClearPendingIRQ(FDCAN1_IT0_IRQn);
    NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

    CAN_INST->CCCR &= ~FDCAN_CCCR_INIT;
    return wait_cccr(FDCAN_CCCR_INIT, false);
}

void fdcan_deinit(void)
{
    NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_FDCAN);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_FDCAN);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_FDCAN);
    NVIC_ClearPendingIRQ(FDCAN1_IT0_IRQn);
}

void FDCAN1_IT0_IRQHandler(void)
{
    CAN_INST->IR = FDCAN_IR_RF0N | FDCAN_IR_RF0L | FDCAN_IR_RF0F;
    uint32_t st;
    while (((st = CAN_INST->RXF0S) & FDCAN_RXF0S_F0FL_Msk) != 0u) {
        uint32_t gi = (st & FDCAN_RXF0S_F0GI_Msk) >> FDCAN_RXF0S_F0GI_Pos;
        const volatile uint32_t *e = (const volatile uint32_t *)(MRAM_RF0 + gi * ELEM_SIZE);
        uint32_t r0 = e[0], r1 = e[1];
        uint32_t head = s_head;
        if (((head + 1u) & (RX_RING_SIZE - 1u)) != s_tail && (r0 & (1UL << 30)) == 0u) {
            can_frame_t *f = &s_ring[head];
            f->id = (uint16_t)((r0 >> 18) & 0x7FFu);
            uint32_t dlc = (r1 >> 16) & 0xFu;
            f->dlc = (uint8_t)(dlc > 8u ? 8u : dlc);
            uint32_t d0 = e[2], d1 = e[3];
            memcpy(&f->data[0], &d0, 4);
            memcpy(&f->data[4], &d1, 4);
            s_head = (head + 1u) & (RX_RING_SIZE - 1u);
        } else {
            s_overruns++;
        }
        CAN_INST->RXF0A = gi;
    }
    if ((st & FDCAN_RXF0S_RF0L_Msk) != 0u) {
        s_overruns++;
    }
}

bool fdcan_rx(can_frame_t *f)
{
    uint32_t tail = s_tail;
    if (tail == s_head) {
        return false;
    }
    *f = s_ring[tail];
    s_tail = (tail + 1u) & (RX_RING_SIZE - 1u);
    return true;
}

bool fdcan_tx(const can_frame_t *f)
{
    uint32_t t0 = clock_ms();
    while ((CAN_INST->TXFQS & FDCAN_TXFQS_TFQF_Msk) != 0u) {
        if ((CAN_INST->CCCR & FDCAN_CCCR_INIT) != 0u || (clock_ms() - t0) > 10u) {
            return false;
        }
    }
    uint32_t pi = (CAN_INST->TXFQS & FDCAN_TXFQS_TFQPI_Msk) >> FDCAN_TXFQS_TFQPI_Pos;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM_TXB + pi * ELEM_SIZE);
    uint32_t d0, d1;
    memcpy(&d0, &f->data[0], 4);
    memcpy(&d1, &f->data[4], 4);
    e[0] = ((uint32_t)f->id & 0x7FFu) << 18;
    e[1] = ((uint32_t)f->dlc & 0xFu) << 16;
    e[2] = d0;
    e[3] = d1;
    CAN_INST->TXBAR = 1UL << pi;
    return true;
}

bool fdcan_tx_idle(void)
{
    return CAN_INST->TXBRP == 0u || (CAN_INST->CCCR & FDCAN_CCCR_INIT) != 0u;
}

void fdcan_poll(void)
{
    /* Bus-off: phần cứng set INIT. Xóa INIT để bắt đầu phục hồi (128 × 11 bit lặn). */
    if ((CAN_INST->PSR & FDCAN_PSR_BO) != 0u && (CAN_INST->CCCR & FDCAN_CCCR_INIT) != 0u) {
        CAN_INST->CCCR &= ~FDCAN_CCCR_INIT;
    }
}
