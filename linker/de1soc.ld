OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)



MEMORY {
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x40000000
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

_stack_size = 0x2000;   /* 8KB for each stack */
_bank_size = 0x80; /*128 tasks */

_heap_offset = 0x200000; /* 1MB offset */
_heap_size = 0x01000000; /* 16MB heap */
_capture_region = 0x80;

SECTIONS {
    .pc_bank ORIGIN(SDRAM) (NOLOAD) : {
        
    }

    .task_bank ORIGIN(SDRAM) (NOLOAD) : { /* Max 128 tasks */
        . = ALIGN(8);
        _bank_start = .;
        . = . + _heap_offset;
        _bank_end = .;
    }

    .heap _bank_end (NOLOAD) : {
        . = ALIGN(8);
        _heap_start = .;
        . = . + _heap_size;
        _heap_end = .;
    } > SDRAM


    .status ORIGIN(OCRAM) (NOLOAD) : {
        _status_base = .;
        . = . + _capture_region;
    } > OCRAM

    ._vectors ORIGIN(OCRAM) + _capture_region : {
        KEEP(*(._vectors))
        . = ALIGN(4);
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

    /* OCRAM stacks */
    . = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 4);

    _sys_stack_top = . + (_stack_size * 1);
    _svc_stack_top = . + (_stack_size * 2);
    _irq_stack_top = . + (_stack_size * 3);
    _fiq_stack_top = . + (_stack_size * 4);

}
