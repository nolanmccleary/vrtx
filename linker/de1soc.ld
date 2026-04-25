OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset)

MEMORY {
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

_stack_size = 0x2000;   /* 8KB for each mode stack */

SECTIONS {
    .vectors ORIGIN(OCRAM) : {
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
    _heap_end   = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 3);

    /* Mode stacks at top of OCRAM — grow downward from their tops */
    _irq_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 1);
    _svc_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 2);
    _sys_stack_top  = ORIGIN(OCRAM) + LENGTH(OCRAM) - (_stack_size * 3);
}
