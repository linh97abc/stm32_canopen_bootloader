/**
 * Startup tối giản cho bootloader: vector table chỉ tới ngắt IT0 của FDCAN
 * mà board dùng (FDCAN1: IRQ 21, FDCAN2: 86, FDCAN3: 88).
 */
#include <stdint.h>

#include "board.h"
#include "stm32g4xx.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);
void Reset_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
void BOARD_CAN_IRQHandler(void);

__attribute__((noreturn)) static void Fault_Handler(void)
{
    NVIC_SystemReset();
}

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0u;
    }
    /* Bootloader build với -mfloat-abi=soft: không bật FPU, app tự bật. */
    (void)main();
    Fault_Handler();
}

#define VECTORS_N (16 + BOARD_CAN_IRQn + 1)

__attribute__((section(".isr_vector"), used))
const void *const g_vectors[VECTORS_N] = {
    [0] = &_estack,
    [1] = (const void *)Reset_Handler,
    [2] = (const void *)NMI_Handler,
    [3] = (const void *)HardFault_Handler,
    [4] = (const void *)Fault_Handler,  /* MemManage */
    [5] = (const void *)Fault_Handler,  /* BusFault */
    [6] = (const void *)Fault_Handler,  /* UsageFault */
    [11] = (const void *)Fault_Handler, /* SVC */
    [14] = (const void *)Fault_Handler, /* PendSV */
    [15] = (const void *)SysTick_Handler,
    [16 + BOARD_CAN_IRQn] = (const void *)BOARD_CAN_IRQHandler,
};

void HardFault_Handler(void) { Fault_Handler(); }
