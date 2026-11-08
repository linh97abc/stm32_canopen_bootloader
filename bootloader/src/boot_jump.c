/** Nhảy từ bootloader sang app (phụ thuộc phần cứng, tách riêng để test host). */
#include "boot_jump.h"

#include "pds/pds_layout.h"
#include "stm32g4xx.h"

#define SRAM_TOTAL (SRAM1_SIZE_MAX + SRAM2_SIZE + CCMSRAM_SIZE) /* CCM được alias liền sau SRAM2 */

bool boot_vectors_ok(void)
{
    uint32_t sp = *(const volatile uint32_t *)PDS_APP_START;
    uint32_t pc = *(const volatile uint32_t *)(PDS_APP_START + 4u);
    bool sp_ok = ((sp & 3u) == 0u) &&
                 ((sp > SRAM1_BASE && sp <= SRAM1_BASE + SRAM_TOTAL) ||
                  (sp > CCMSRAM_BASE && sp <= CCMSRAM_BASE + CCMSRAM_SIZE));
    bool pc_ok = ((pc & 1u) != 0u) && pc > PDS_APP_START && pc < pds_layout_app_end();
    return sp_ok && pc_ok;
}

void boot_jump_to_app(void)
{
    if (!boot_vectors_ok()) {
        return;
    }
    uint32_t sp = *(const volatile uint32_t *)PDS_APP_START;
    uint32_t pc = *(const volatile uint32_t *)(PDS_APP_START + 4u);

    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->VAL = 0u;
    for (unsigned i = 0; i < sizeof NVIC->ICER / sizeof NVIC->ICER[0]; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    SCB->VTOR = PDS_APP_START;
    __set_CONTROL(0u); /* MSP, privileged */
    __DSB();
    __ISB();

    /* Đặt MSP rồi nhảy luôn trong asm để compiler không dùng stack giữa chừng. */
    __asm volatile(
        "msr msp, %0\n"
        "cpsie i\n"
        "bx %1\n"
        :
        : "r"(sp), "r"(pc)
        : "memory");
    __builtin_unreachable();
}
