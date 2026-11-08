/* Linker script bootloader, tiền xử lý bằng cpp để dùng chung blc_layout.h. */
#include "blc/blc_layout.h"

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = BLC_BOOTLOADER_START, LENGTH = BLC_BOOTLOADER_SIZE
    RAM   (rwx) : ORIGIN = BLC_BL_RAM_START,     LENGTH = BLC_BL_RAM_SIZE
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x1000;

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    ASSERT(_ebss + _min_stack <= _estack, "RAM không đủ cho stack")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
