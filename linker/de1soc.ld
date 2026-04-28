OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)

MEMORY {
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

_stack_size = 0x2000;   /* 8KB for each mode stack */

SECTIONS {
    ._vectors ORIGIN(OCRAM) : {
        KEEP(*(.vectors))
    } > OCRAM

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > OCRAM

    .data : {
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > OCRAM

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > OCRAM

    . = ALIGN(8);
    _heap_start = .;
    _heap_end       = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 4);


    /*for now ignore usr, abort, undef*/
    _sys_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 3);
    _svc_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 2);
    _irq_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 1);
    _fiq_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 0);
}
