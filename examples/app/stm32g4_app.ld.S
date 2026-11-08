/*
 * Linker script mẫu cho app chạy sau bootloader.
 * Tiền xử lý:  arm-none-eabi-gcc -E -P -x c -DBLC_LINKER_SCRIPT -DSTM32G474xx \
 *              -I<repo>/common/include stm32g4_app.ld.S -o app.ld
 *
 * Nếu dùng file .ld do CubeMX sinh, chỉ cần:
 *   - FLASH ORIGIN = BLC_APP_START, LENGTH giảm tương ứng.
 *   - Thêm ngay sau .isr_vector:
 *       .app_header (ORIGIN(FLASH) + 0x200) : { LONG(0) LONG(0) LONG(0) LONG(0)
 *                                                LONG(0) LONG(0) LONG(0) } >FLASH
 */
#include "blc/blc_layout.h"

#ifndef APP_FLASH_SIZE_KB
#define APP_FLASH_SIZE_KB 512   /* tổng flash của chip */
#endif
#ifndef APP_RAM_SIZE_KB
#define APP_RAM_SIZE_KB   96    /* SRAM1 + SRAM2 (G474: 80 + 16) */
#endif

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = BLC_APP_START, LENGTH = BLC_FLASH_BASE + APP_FLASH_SIZE_KB * 1024 - BLC_APP_START
    RAM   (rwx) : ORIGIN = 0x20000000,    LENGTH = APP_RAM_SIZE_KB * 1024
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x800;

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) } >FLASH
    ASSERT(SIZEOF(.isr_vector) <= 0x200, "vector table đè lên app header")

    /* Header image tại APP_START + 0x200 (design.md mục 5.1): chừa 28 byte 0,
       tools/blc_image.py sign điền lúc post-build. */
    .app_header (ORIGIN(FLASH) + 0x200) :
    {
        LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.glue_7) *(.glue_7t) *(.eh_frame)
        KEEP(*(.init)) KEEP(*(.fini))
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM       : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
    .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
    .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } >RAM AT> FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } >RAM

    ._user_heap_stack (NOLOAD) : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .); . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; . = ALIGN(8); } >RAM

    /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
