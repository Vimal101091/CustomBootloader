ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx)  : ORIGIN = flash_base, LENGTH = flash_size
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM
}
ASSERT((flash_base & 511) == 0, "Vector table must be 512-byte aligned")
ASSERT(_bss_end + 4096 <= _stack_top, "Reserve at least 4 KiB for the stack")
