/* Linker script bootloader, tiền xử lý bằng cpp để dùng chung pds_layout.h. */
#include "pds/pds_layout.h"

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = PDS_BOOTLOADER_START, LENGTH = PDS_BOOTLOADER_SIZE
    RAM   (rwx) : ORIGIN = PDS_BL_RAM_START,     LENGTH = PDS_BL_RAM_SIZE
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
